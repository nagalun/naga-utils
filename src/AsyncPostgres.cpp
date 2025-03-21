#include "AsyncPostgres.hpp"
#include "OpCancelledException.hpp"
#include "Poll.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <chrono>

#include <explints.hpp>
#include <TimedCallbacks.hpp>
#include <utils.hpp>
#include <stringparser.hpp>

using namespace nev;
using namespace std::chrono_literals;

bool AsyncPostgres::QuerySharedPtrComparator::operator()(const ll::shared_ptr<Query>& lhs, const ll::shared_ptr<Query>& rhs) const {
	return *lhs > *rhs;
}

static void getStringPointers(const std::unordered_map<std::string, std::string>& str, const char ** k, const char ** v) {
	sz_t i = 0;
	for (auto it = str.begin(); it != str.end(); ++it) {
		k[i] = it->first.c_str();
		v[i] = it->second.c_str();
		++i;
	}

	// write null termination
	k[i] = v[i] = nullptr;
}

static int postgresEvToUv(int ev) {
	using Evt = nev::Poll::Evt;
	switch (ev) { // the pg enum is not composed of powers of two
		case PGRES_POLLING_READING:
			return Evt::READABLE;

		case PGRES_POLLING_WRITING:
			return Evt::WRITABLE;

		default:
			return 0;
	}
}

AsyncPostgres::AsyncPostgres(Loop& loop, TimedCallbacks& tc)
: loop(loop),
  tc(tc),
  nextCommandCaller(),
  pgConn(nullptr, PQfinish),
  pSock(),
  queries(QuerySharedPtrComparator()),
  currentQuery(queries.end()),
  notifFunc(nullptr),
  connChangeFunc(nullptr),
  stopOnceEmpty(false),
  busy(false),
  awaitingResponse(false),
  autoReconnect(true),
  debugPrinting(false) {

	nextCommandCaller = loop.async([this] (nev::Async&) {
		processNextCommand();
	}, true);
}

void AsyncPostgres::connect(std::unordered_map<std::string, std::string> connParams, bool expandDbname) {
	std::vector<const char*> keywords(connParams.size() + 1); // +1 for null termination
	std::vector<const char*> values(connParams.size() + 1);
	getStringPointers(connParams, keywords.data(), values.data());

	pgConn.reset(PQconnectStartParams(keywords.data(), values.data(), expandDbname));
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
	busy = false;
	stopOnceEmpty = false;

	if (notify && connChangeFunc) {
		connChangeFunc(getStatus());
	}
}

void AsyncPostgres::setAutoReconnect(bool state) {
	autoReconnect = state;
}

void AsyncPostgres::setDebugPrinting(bool state) {
	debugPrinting = state;
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
	if (isConnected()) {
		return;
	}

	pSock = nullptr;
	if (connChangeFunc) {
		connChangeFunc(getStatus());
	}

	if (isAutoReconnectEnabled()) {
		reconnectTimer = tc.timer([this] {
			if (!reconnect()) {
				std::cerr << "Couldn't reconnect to DB! (" << getLastErrorFirstLine() << "), retrying." << std::endl;
				return true;
			}

			return false;
		}, 2000ms);
	}
}

template<PostgresPollingStatusType(*PollFunc)(PGconn *)>
void AsyncPostgres::pollConnection() {
	if (PQsetnonblocking(pgConn.get(), true) == -1) {
		throwLastError();
	}

	busy = true; // queue queries until we're completely connected

	pSock = loop.poll(PQsocket(pgConn.get()));
	pSock->start(Poll::Evt::WRITABLE, [this] (Poll& p, int s, int e) {
		int newEvs = PollFunc(pgConn.get());
		switch (newEvs) {
			case PGRES_POLLING_FAILED:
				std::cerr << "PGRES_POLLING_FAILED: " << getLastErrorFirstLine() << std::endl;
				busy = false;
				maybeSignalDisconnectionAndReconnect();
				break;

			case PGRES_POLLING_OK:
				p.start(Poll::Evt::READABLE | Poll::Evt::WRITABLE, [this] (Poll& p, int s, int e) {
					socketCallback(p, s, e);
				});

				if (connChangeFunc) {
					connChangeFunc(getStatus());
				}

				signalCompletion();
				break;

			case PGRES_POLLING_WRITING:
			case PGRES_POLLING_READING:
				p.change(postgresEvToUv(newEvs));
				break;
		}
	});
}

void AsyncPostgres::signalCompletion() {
	nextCommandCaller->send();
}

void AsyncPostgres::processNextCommand() {
	busy = !queries.empty();
	if (!busy) {
		if (stopOnceEmpty) {
			disconnect();
		}

		return;
	}

	currentQuery = queries.begin();
	if (debugPrinting) {
		(*currentQuery)->print();
	}

	if ((*currentQuery)->send(pgConn.get())) {
		awaitingResponse = true;
		return;
	}

	// if query sending failed
	currentQuery = queries.end();
	printLastError();
	if (!isConnected()) {
		maybeSignalDisconnectionAndReconnect();
		return;
	}

	// this should never happen, but just in case try again in 5 secs
	qRetryTimer = tc.timer([this] {
		processNextCommand();
		return false;
	}, 5000ms);
}

// won't work yet with single row mode cb
void AsyncPostgres::currentCommandReturnedResult(PGresult * r, bool finished) {
	// XXX: no check if empty, shouldn't happen anyways
	if (finished) {
		// if (debugPrinting) {
		// 	std::cout << "[SQL] Query finished." << std::endl;
		// }

		(*currentQuery)->done(Result(r, nullptr));
		queries.erase(currentQuery);

		awaitingResponse = false;
		currentQuery = queries.end();
		signalCompletion();
	} else {
		// the connection pointer is needed to copy data
		(*currentQuery)->done(Result(r, pgConn.get()));
	}
}

void AsyncPostgres::manageSocketEvents(bool needsWrite) {
	if (!PQisBusy(pgConn.get())) {
		while (PGresult * r = PQgetResult(pgConn.get())) {
			bool finished = true;

			ExecStatusType s = PQresultStatus(r);
			//std::cout << "Popcb " << PQresStatus(s) << std::endl;
			switch (s) { // FIXME?: make it possible to enable single row mode
				case PGRES_COPY_IN:
				case PGRES_COPY_OUT:
					finished = false; // user has to copy data still
					break;

				case PGRES_BAD_RESPONSE:
				case PGRES_FATAL_ERROR:
					std::cerr << "[Postgre/manageSocketEvents()]: Bad result status " << PQresStatus(s) << std::endl;
					printLastError();
					break;

				default:
					break;
			}

			currentCommandReturnedResult(r, finished);
		}
	}

	// always listen for read because the server can send us notifs at any time
	int evs = Poll::Evt::READABLE;
	evs |= needsWrite ? Poll::Evt::WRITABLE : 0;

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

void AsyncPostgres::socketCallback(Poll& p, int s, int e) {
	bool needsWrite = false;

	if (e & Poll::Evt::READABLE) {
		if (!PQconsumeInput(pgConn.get())) {
			printLastError();
			if (!isConnected()) {
				maybeSignalDisconnectionAndReconnect();
				return;
			}
		}

		while (PGnotify * np = PQnotifies(pgConn.get())) {
			Notification n{np};
			if (notifFunc) {
				notifFunc(std::move(n));
			}
		}
	}

	if (e & Poll::Evt::WRITABLE) {
		needsWrite = PQflush(pgConn.get());
	}

	manageSocketEvents(needsWrite);
}

AsyncPostgres::Query::Query(AsyncPostgres& ap, int prio, std::stop_token st, std::string cmd, const char * const * vals, const int * lens, const int * fmts, int n)
: ap(ap),
  stopCb(std::move(st), [this] { this->ap.cancelQuery(*this); }),
  command(std::move(cmd)),
  coro(nullptr),
  values(vals),
  lengths(lens),
  formats(fmts),
  nParams(n),
  priority(prio),
  expectsResults(true),
  cancelled(false) { }

AsyncPostgres::Query::~Query() {
	if (expectsResults) {
		// tell callback we couldn't complete the request
		done(AsyncPostgres::Result(nullptr, nullptr));
	}
}

void AsyncPostgres::Query::setPointers(const char* const* _vals, const int* _lengths, const int* _formats) {
	values = _vals;
	lengths = _lengths;
	formats = _formats;
}

void AsyncPostgres::Query::markCancelled() {
	cancelled = true;
}

bool AsyncPostgres::Query::isDone() const {
	return !expectsResults;
}

void AsyncPostgres::Query::then(std::function<void(AsyncPostgres::Result)> f) {
	if (res) {
		f(std::move(res));
	}

	if (expectsResults) {
		onDone = std::move(f);
	}
}

bool AsyncPostgres::Query::await_ready() const noexcept {
	return cancelled || res || isDone();
}

void AsyncPostgres::Query::await_suspend(std::coroutine_handle<> h) {
	coro = std::move(h);
	// doesn't get called for some dumb reason
	// std::cout << "suspend\n";
	// then([this, h{std::move(h)}](Result r) {
	// 	h.resume();
	// 	std::cout << "resume\n";
	// 	onDone = nullptr; // don't resume twice if more data is coming
	// });
}

AsyncPostgres::Result AsyncPostgres::Query::await_resume() {
	if (cancelled) {
		throw OpCancelledException{};
	}

	if (!res.success()) {
		res.throwStatus();
	}

	return std::move(res);
}

void AsyncPostgres::Query::print() {
	std::cout << "[SQL n=" << nParams << "] " << command << std::endl;
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
	expectsResults = r.expectMoreResults();
	res = std::move(r);
	if (onDone) {
		onDone(std::move(res));
		res = {};
		if (!expectsResults) {
			onDone = nullptr; // make it impossible to double-complete
		}
	}
	if (coro) {
		coro.resume();
		coro = nullptr;
	}
}

AsyncPostgres::QueryQueue::const_iterator AsyncPostgres::Query::getQueueIterator() const {
	return queue_it;
}

void AsyncPostgres::Query::setQueueIterator(AsyncPostgres::QueryQueue::const_iterator it) {
	queue_it = it;
}

AwaiterProxy<AsyncPostgres::Query> operator co_await(ll::shared_ptr<AsyncPostgres::Query> q) {
	// this looks scary, but a temporary object's lifetime is guaranteed to live for the full co_await expression,
	// which q indirectly is part of
	return {*q};
}

AsyncPostgres::Result::Result()
: Result(nullptr, nullptr) { }

AsyncPostgres::Result::Result(PGresult * r, PGconn * c)
: pgResult(r, PQclear),
  conn(c) { }

sz_t AsyncPostgres::Result::numAffected() const {
	if (!pgResult) {
		return 0;
	}

	std::string_view affected(PQcmdTuples(pgResult.get()));

	return affected.size() ? fromString<sz_t>(affected) : 0;
}

sz_t AsyncPostgres::Result::size() const {
	return pgResult ? PQntuples(pgResult.get()) : 0;
}

sz_t AsyncPostgres::Result::rowSize() const {
	return pgResult ? PQnfields(pgResult.get()) : 0;
}

bool AsyncPostgres::Result::success() const {
	switch (getStatus()) {
		case PGRES_COPY_OUT:
		case PGRES_COPY_IN:
		case PGRES_COMMAND_OK:
		case PGRES_TUPLES_OK:
		case PGRES_SINGLE_TUPLE:
			return true;

		default:
			break;
	}

	return false;
}

bool AsyncPostgres::Result::expectMoreResults() const {
	switch (getStatus()) {
		case PGRES_COPY_OUT:
		case PGRES_COPY_IN:
		case PGRES_SINGLE_TUPLE:
			return true;

		default:
			break;
	}

	return false;
}

ExecStatusType AsyncPostgres::Result::getStatus() const {
	return pgResult ? PQresultStatus(pgResult.get()) : PGRES_BAD_RESPONSE;
}

const char* AsyncPostgres::Result::getErrorMessage() const {
	return pgResult ? PQresultErrorMessage(pgResult.get()) : "no result";
}

void AsyncPostgres::Result::throwStatus() const {
	throw Error{getStatus(), getErrorMessage()};
}

bool AsyncPostgres::Result::canCopyTo() const {
	return getStatus() == PGRES_COPY_IN;
}

bool AsyncPostgres::Result::blockingCopy(const char * buffer, sz_t nbytes) {
	if (PQsetnonblocking(conn, false) == -1) {
		return false;
	}

	int s = PQputCopyData(conn, buffer, nbytes);

	PQsetnonblocking(conn, true); // this should never return -1
	return s == 1;
}

bool AsyncPostgres::Result::blockingCopyEnd(const char * err) {
	if (PQsetnonblocking(conn, false) == -1) {
		return false;
	}

	int s = PQputCopyEnd(conn, err);

	PQsetnonblocking(conn, true);
	return s == 1;
}

const char* AsyncPostgres::Result::Error::what() const noexcept {
	return errMsg.c_str();
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

AsyncPostgres::Result::Error::Error(ExecStatusType errCode, std::string errMsg)
: errCode(errCode),
  errMsg(std::move(errMsg)) { }

ExecStatusType AsyncPostgres::Result::Error::code() const noexcept {
	return errCode;
}

const char* AsyncPostgres::Result::Error::codeStr() const noexcept {
	return PQresStatus(errCode);
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
