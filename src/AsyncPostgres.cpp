#include "AsyncPostgres.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

#include <explints.hpp>
#include <TimedCallbacks.hpp>
#include <utils.hpp>

#include <uWS.h>

bool AsyncPostgres::QuerySharedPtrComparator::operator()(const ll::shared_ptr<Query>& lhs, const ll::shared_ptr<Query>& rhs) const {
	return *lhs > *rhs;
}

void getStringPointers(const std::unordered_map<std::string, std::string>& str, const char ** k, const char ** v) {
	sz_t i = 0;
	for (auto it = str.begin(); it != str.end(); ++it) {
		k[i] = it->first.c_str();
		v[i] = it->second.c_str();
		++i;
	}

	// write null termination
	k[i] = v[i] = nullptr;
}

int postgresEvToUv(int ev) {
	switch (ev) {
		case PGRES_POLLING_READING:
			return UV_READABLE;

		case PGRES_POLLING_WRITING:
			return UV_WRITABLE;

		default:
			return 0;
	}
}

class PostgresSocket : uS::Poll {
	AsyncPostgres * ap;
	void (*cb)(AsyncPostgres *, PostgresSocket *, int, int);

public:
	PostgresSocket(AsyncPostgres * ap, int socketfd)
	: Poll(ap->loop, socketfd),
	  ap(ap) { }

	void start(int events, void (*callback)(AsyncPostgres *, PostgresSocket *, int status, int events)) {
		cb = callback;
		Poll::setCb([] (Poll * p, int s, int e) {
			PostgresSocket * cs = static_cast<PostgresSocket *>(p);
			cs->cb(cs->ap, cs, s, e);
		});

		Poll::start(ap->loop, this, events);
	}

	void change(int events) {
		Poll::change(ap->loop, this, events);
	}

	void close() {
		Poll::stop(ap->loop);
		Poll::close(ap->loop, [] (Poll * p) {
			delete static_cast<PostgresSocket *>(p);
		});
	}

	void setCb(void (*callback)(AsyncPostgres *, PostgresSocket *, int status, int events)) {
		cb = callback;
	}
};

AsyncPostgres::AsyncPostgres(uS::Loop * loop, TimedCallbacks& tc)
: loop(loop),
  tc(tc),
  nextCommandCaller(nullptr, [] (uS::Async * a) { a->close(); }),
  pgConn(nullptr, PQfinish),
  pSock(nullptr, [] (PostgresSocket * p) { p->close(); }),
  queries(QuerySharedPtrComparator()),
  currentQuery(queries.end()),
  notifFunc([] (Notification) {}),
  connChangeFunc([] (ConnStatusType) {}),
  stopOnceEmpty(false),
  busy(false),
  awaitingResponse(false),
  autoReconnect(true) { }

void AsyncPostgres::connect(std::unordered_map<std::string, std::string> connParams, bool expandDbname) {
	const char * keywords[connParams.size() + 1]; // +1 for null termination
	const char * values[connParams.size() + 1];
	getStringPointers(connParams, keywords, values);

	prepareForConnection();
	pgConn.reset(PQconnectStartParams(keywords, values, expandDbname));
	if (!pgConn) {
		throw std::bad_alloc();
	}

	if (getStatus() == CONNECTION_BAD) {
		maybeSignalDisconnectionAndReconnect();
		return;
	}

	pollConnection<PQconnectPoll>();
}

bool AsyncPostgres::reconnect() {
	if (pgConn && PQresetStart(pgConn.get())) {
		pollConnection<PQresetPoll>();
		return true;
	}

	return false;
}

void AsyncPostgres::lazyDisconnect() {
	stopOnceEmpty = true;
	if (!busy) {
		disconnect();
	}
}

void AsyncPostgres::disconnect() {
	bool notify = isConnected();

	pSock = nullptr;
	pgConn = nullptr;
	nextCommandCaller = nullptr;
	busy = false;
	stopOnceEmpty = false;

	if (notify) {
		connChangeFunc(getStatus());
	}
}

void AsyncPostgres::setAutoReconnect(bool state) {
	autoReconnect = state;
}

bool AsyncPostgres::cancelQuery(Query& q) {
	if (q.isDone()) {
		return false;
	}

	auto it = q.getQueueIterator();
	if (it == currentQuery && awaitingResponse) {
		// query is already sent (or being sent) to postgres, cancel request might fail
		return PQrequestCancel(pgConn.get()) == 1;
		// return false;
	}

	queries.erase(it);
	return true;
}

bool AsyncPostgres::isConnected() const {
	return getStatus() == CONNECTION_OK;
}

ConnStatusType AsyncPostgres::getStatus() const {
	return pgConn ? PQstatus(pgConn.get()) : CONNECTION_BAD;
}

sz_t AsyncPostgres::queuedQueries() const {
	return queries.size();
}

bool AsyncPostgres::isAutoReconnectEnabled() const {
	return autoReconnect;
}

int AsyncPostgres::backendPid() const {
	return pgConn ? PQbackendPID(pgConn.get()) : 0;
}

void AsyncPostgres::onConnectionStateChange(std::function<void(ConnStatusType)> f) {
	connChangeFunc = std::move(f);
}

void AsyncPostgres::onNotification(std::function<void(Notification)> f) {
	notifFunc = std::move(f);
}

void AsyncPostgres::maybeSignalDisconnectionAndReconnect() {
	if (!isConnected()) {
		pSock = nullptr;
		connChangeFunc(getStatus());

		if (isAutoReconnectEnabled()) {
			tc.startTimer([this] {
				if (!reconnect()) {
					std::cerr << "Couldn't reconnect to DB! (" << getLastErrorFirstLine() << "), retrying." << std::endl;
					return true;
				}

				return false;
			}, 2000);
		}
	}
}

void AsyncPostgres::prepareForConnection() {
	nextCommandCaller.reset(new uS::Async(loop));
	nextCommandCaller->setData(this);
	nextCommandCaller->start(AsyncPostgres::nextCmdCallerCallback);
}

template<PostgresPollingStatusType(*PollFunc)(PGconn *)>
void AsyncPostgres::pollConnection() {
	if (PQsetnonblocking(pgConn.get(), true) == -1) {
		throwLastError();
	}

	busy = true; // queue queries until we're completely connected

	pSock.reset(new PostgresSocket(this, PQsocket(pgConn.get())));
	pSock->start(UV_WRITABLE, +[] (AsyncPostgres * ap, PostgresSocket * ps, int s, int e) {
		int newEvs = PollFunc(ap->pgConn.get());
		switch (newEvs) {
			case PGRES_POLLING_FAILED:
				std::cerr << "PGRES_POLLING_FAILED: " << ap->getLastErrorFirstLine() << std::endl;
				ap->busy = false;
				ap->maybeSignalDisconnectionAndReconnect();
				break;

			case PGRES_POLLING_OK:
				ps->setCb(AsyncPostgres::socketCallback);
				ap->connChangeFunc(ap->getStatus());
				ap->busy = false;
				ap->signalCompletion();
				break;

			case PGRES_POLLING_WRITING:
			case PGRES_POLLING_READING:
				ps->change(postgresEvToUv(newEvs));
				break;
		}
	});
}

void AsyncPostgres::signalCompletion() {
	nextCommandCaller->send();
}

void AsyncPostgres::processNextCommand() {
	if (!queries.empty()) {
		busy = true;
		currentQuery = queries.begin();
		if (!(*currentQuery)->send(pgConn.get())) {
			currentQuery = queries.end();
			printLastError();
			if (isConnected()) {
				// this should never happen, but just in case try again in 5 secs
				tc.startTimer([this] {
					processNextCommand();
					return false;
				}, 5000);
			} else {
				maybeSignalDisconnectionAndReconnect();
			}
		} else {
			awaitingResponse = true;
		}
	} else {
		busy = false;
		if (stopOnceEmpty) {
			disconnect();
		}
	}
}

// won't work yet with single row mode cb
void AsyncPostgres::currentCommandFinished(PGresult * r) {
	// XXX: no check if empty, shouldn't happen anyways
	(*currentQuery)->done(r);
	queries.erase(currentQuery);

	awaitingResponse = false;
	currentQuery = queries.end();
	signalCompletion();
}

void AsyncPostgres::manageSocketEvents(bool needsWrite) {
	if (!PQisBusy(pgConn.get())) {
		while (PGresult * r = PQgetResult(pgConn.get())) {
			ExecStatusType s = PQresultStatus(r);
			//std::cout << "Popcb " << PQresStatus(s) << std::endl;
			switch (s) { // FIXME?: make it possible to enable single row mode
				case PGRES_BAD_RESPONSE:
				case PGRES_FATAL_ERROR:
					std::cerr << "[Postgre/manageSocketEvents()]: Bad result status " << PQresStatus(s) << std::endl;
					printLastError();
					break;
			}

			currentCommandFinished(r);
		}
	}

	// always listen for read because the server can send us notifs at any time
	int evs = UV_READABLE;
	evs |= needsWrite ? UV_WRITABLE : 0;

	pSock->change(evs);
}

std::string_view AsyncPostgres::getLastErrorFirstLine() {
	std::string_view err(PQerrorMessage(pgConn.get()));

	return err.substr(0, err.find('\n'));
}

void AsyncPostgres::printLastError() {
	std::cerr << PQerrorMessage(pgConn.get()) << std::endl;
}

void AsyncPostgres::throwLastError() {
	throw std::runtime_error(PQerrorMessage(pgConn.get()));
}

void AsyncPostgres::socketCallback(AsyncPostgres * ap, PostgresSocket * ps, int s, int e) {
	bool needsWrite = false;

	if (e & UV_READABLE) {
		if (!PQconsumeInput(ap->pgConn.get())) {
			ap->printLastError();
			if (!ap->isConnected()) {
				ap->maybeSignalDisconnectionAndReconnect();
				return;
			}
		}

		while (PGnotify * p = PQnotifies(ap->pgConn.get())) {
			ap->notifFunc(p);
		}
	}

	if (e & UV_WRITABLE) {
		needsWrite = PQflush(ap->pgConn.get());
	}

	ap->manageSocketEvents(needsWrite);
}

void AsyncPostgres::nextCmdCallerCallback(uS::Async * a) {
	AsyncPostgres * ap = static_cast<AsyncPostgres *>(a->getData());
	ap->processNextCommand();
}


AsyncPostgres::Query::Query(int prio, std::string cmd, const char ** vals, const int * lens, const int * fmts, int n)
: command(std::move(cmd)),
  onDone([] (AsyncPostgres::Result) {}),
  values(vals),
  lengths(lens),
  formats(fmts),
  nParams(n),
  priority(prio) { }

AsyncPostgres::Query::~Query() {
	if (onDone) {
		// tell callback we couldn't complete the request
		onDone(AsyncPostgres::Result(nullptr));
	}
}

bool AsyncPostgres::Query::isDone() const {
	return !onDone;
}

void AsyncPostgres::Query::then(std::function<void(AsyncPostgres::Result)> f) {
	onDone = std::move(f);
}

bool AsyncPostgres::Query::operator>(const Query& q) const {
	return priority > q.priority;
}

bool AsyncPostgres::Query::operator<(const Query& q) const {
	return priority < q.priority;
}

int AsyncPostgres::Query::send(PGconn * conn) {
	return PQsendQueryParams(conn, command.c_str(), nParams,
		nullptr, values, lengths, formats, 0);
}

void AsyncPostgres::Query::done(AsyncPostgres::Result r) {
	if (onDone) {
		onDone(std::move(r));
		onDone = nullptr; // make it impossible to double-complete
	}
}

AsyncPostgres::QueryQueue::const_iterator AsyncPostgres::Query::getQueueIterator() const {
	return queue_it;
}

void AsyncPostgres::Query::setQueueIterator(AsyncPostgres::QueryQueue::const_iterator it) {
	queue_it = it;
}

AsyncPostgres::Result::Result(PGresult * r)
: pgResult(r, PQclear) { }

sz_t AsyncPostgres::Result::size() const {
	return pgResult ? PQntuples(pgResult.get()) : 0;
}

sz_t AsyncPostgres::Result::rowSize() const {
	return pgResult ? PQnfields(pgResult.get()) : 0;
}

bool AsyncPostgres::Result::success() const {
	if (!pgResult.get()) { return false; }

	switch (PQresultStatus(pgResult.get())) {
		case PGRES_COMMAND_OK:
		case PGRES_TUPLES_OK:
		case PGRES_SINGLE_TUPLE:
			return true;
	}

	return false;
}

AsyncPostgres::Result::iterator AsyncPostgres::Result::begin() { return iterator(pgResult.get(), 0); }
AsyncPostgres::Result::iterator AsyncPostgres::Result::end()   { return iterator(pgResult.get(), size()); }

AsyncPostgres::Result::Row AsyncPostgres::Result::operator[](int i) { return Row(pgResult.get(), i); }

AsyncPostgres::Result::operator bool() const { return success(); }


AsyncPostgres::Result::Row::Row(PGresult * r, int i)
: r(r),
  rowIndex(i) { }

int AsyncPostgres::Result::Row::getRow() { return rowIndex; }


AsyncPostgres::Result::Row::ParseException::ParseException(std::vector<std::type_index> ts, std::type_index oldExc, std::string oldMsg)
: message("Row::get<"),
  triedTypes(std::move(ts)),
  causingExceptionType(std::move(oldExc)),
  causingWhat(std::move(oldMsg)) {
	for (sz_t i = 0; i < triedTypes.size(); i++) {
		message += demangle(triedTypes[i]);

		if (i + 1 != triedTypes.size()) {
			message += ", ";
		}
	}

	message += ">() failed! Caused by: " + demangle(causingExceptionType) + ", what(): " + causingWhat;
}

const char * AsyncPostgres::Result::Row::ParseException::what() const noexcept {
	return message.c_str();
}



AsyncPostgres::Result::iterator::iterator(PGresult * r, int i)
: r(r),
  rowIndex(i) { }

AsyncPostgres::Result::iterator& AsyncPostgres::Result::iterator::operator++() {
	++rowIndex;
	return *this;
}

AsyncPostgres::Result::iterator  AsyncPostgres::Result::iterator::operator++(int) {
	auto it = *this;
	++rowIndex;
	return it;
}

AsyncPostgres::Result::iterator& AsyncPostgres::Result::iterator::operator--() {
	--rowIndex;
	return *this;
}

AsyncPostgres::Result::iterator  AsyncPostgres::Result::iterator::operator--(int) {
	auto it = *this;
	--rowIndex;
	return it;
}

int AsyncPostgres::Result::iterator::operator-(iterator it) {
	return rowIndex - it.rowIndex;
}

bool AsyncPostgres::Result::iterator::operator>(iterator it) const {
	return rowIndex > it.rowIndex;
}

bool AsyncPostgres::Result::iterator::operator<(iterator it) const {
	return rowIndex < it.rowIndex;
}

bool AsyncPostgres::Result::iterator::operator==(iterator it) const {
	return rowIndex == it.rowIndex;
}

bool AsyncPostgres::Result::iterator::operator!=(iterator it) const {
	return rowIndex != it.rowIndex;
}

AsyncPostgres::Result::iterator::reference AsyncPostgres::Result::iterator::operator*() const {
	return AsyncPostgres::Result::Row(r, rowIndex);
}


AsyncPostgres::Notification::Notification(PGnotify * n)
: pgNotify(n, [] (PGnotify * n) { PQfreemem(n); }) { }

std::string_view AsyncPostgres::Notification::channelName() const { return pgNotify->relname; }
int              AsyncPostgres::Notification::bePid()       const { return pgNotify->be_pid; }
std::string_view AsyncPostgres::Notification::extra()       const { return pgNotify->extra; }
