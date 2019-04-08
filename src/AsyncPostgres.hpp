#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <tuple>
#include <iterator>
#include <functional>
#include <set>

#include <fwd_uWS.h>
#include <explints.hpp>
#include <tuple.hpp>
#include <shared_ptr_ll.hpp>

#include <postgresql/libpq-fe.h>

class PostgresSocket;
class TimedCallbacks;

class AsyncPostgres {
public:
	class Query;

	struct QuerySharedPtrComparator {
		bool operator()(const ll::shared_ptr<Query>& lhs, const ll::shared_ptr<Query>& rhs) const;
	};

	template<typename... Ts>
	class TemplatedQuery;
	class Result;
	class Notification;
	using QueryQueue = std::multiset<ll::shared_ptr<Query>, QuerySharedPtrComparator>;

private:
	uS::Loop * loop;
	TimedCallbacks& tc;

	std::unique_ptr<uS::Async, void (*)(uS::Async *)> nextCommandCaller;
	std::unique_ptr<PGconn, void (*)(PGconn *)> pgConn;
	std::unique_ptr<PostgresSocket, void (*)(PostgresSocket *)> pSock;
	QueryQueue queries;
	QueryQueue::const_iterator currentQuery; // queries.begin() could change at any time

	std::function<void(Notification)> notifFunc;
	std::function<void(ConnStatusType)> connChangeFunc;
	bool stopOnceEmpty;
	bool busy;
	bool awaitingResponse;
	bool autoReconnect;

public:
	AsyncPostgres(uS::Loop *, TimedCallbacks&);

	void connect(std::unordered_map<std::string, std::string> connParams = {}, bool expandDbname = false);
	bool reconnect(); // reconnects with the same parameters

	void lazyDisconnect(); // will disconnect when the query queue is empty
	void disconnect();

	void setAutoReconnect(bool);

	template<int priority = 0, typename... Ts>
	ll::shared_ptr<Query> query(std::string, Ts&&...);
	// cancel may fail, query callback will still be called, even if cancelled ok
	bool cancelQuery(Query&);

	bool isConnected() const;
	ConnStatusType getStatus() const;
	sz_t queuedQueries() const;
	bool isAutoReconnectEnabled() const;
	int backendPid() const;

	void onConnectionStateChange(std::function<void(ConnStatusType)>);
	void onNotification(std::function<void(Notification)>);

private:
	void maybeSignalDisconnectionAndReconnect();

	void prepareForConnection();
	template<PostgresPollingStatusType(*PollFunc)(PGconn *)>
	void pollConnection();

	void signalCompletion();
	void processNextCommand();
	void currentCommandFinished(PGresult *);
	void manageSocketEvents(bool);

	std::string_view getLastErrorFirstLine();
	void printLastError();
	void throwLastError();

	static void socketCallback(AsyncPostgres *, PostgresSocket *, int, int);
	static void nextCmdCallerCallback(uS::Async *);

	friend PostgresSocket;
};

class AsyncPostgres::Query {
	std::string command;
	std::function<void(Result)> onDone;
	const char ** values;
	const int * lengths;
	const int * formats;
	AsyncPostgres::QueryQueue::const_iterator queue_it;
	int nParams;
	const int priority;

public:
	Query(int prio, std::string, const char **, const int *, const int *, int);
	virtual ~Query();

	bool isDone() const;
	void then(std::function<void(Result)>);

	bool operator>(const Query&) const;
	bool operator<(const Query&) const;

private:
	int send(PGconn *);
	void done(Result);
	AsyncPostgres::QueryQueue::const_iterator getQueueIterator() const;
	void setQueueIterator(AsyncPostgres::QueryQueue::const_iterator);

	friend AsyncPostgres;
};

template<typename... Ts>
class AsyncPostgres::TemplatedQuery : public AsyncPostgres::Query {
	std::tuple<Ts...> valueStorage;
	const char * realValues[sizeof... (Ts)];
	const int realLengths[sizeof... (Ts)];
	const int realFormats[sizeof... (Ts)];

	template<std::size_t... Is>
	TemplatedQuery(std::index_sequence<Is...>, int prio, std::string, Ts&&...);

public:
	TemplatedQuery(int prio, std::string, Ts&&...);
};

class AsyncPostgres::Result {
public:
	class Row;
	class iterator;

private:
	std::unique_ptr<PGresult, void (*)(PGresult *)> pgResult;

	Result(PGresult *);

public:
	sz_t size() const;
	sz_t rowSize() const;
	bool success() const;

	iterator begin();
	iterator end();

	template<typename Func, typename Tuple = typename lambdaToTuple<Func>::type>
	void forEach(Func);

	Row operator[](int);
	operator bool() const;

	friend AsyncPostgres;
};

class AsyncPostgres::Result::Row {
	PGresult * r;
	const int rowIndex;

public:
	Row(PGresult *, int);

	template<typename... Ts>
	std::tuple<Ts...> get();

	int getRow();

private:
	template<typename Tuple, std::size_t... Is>
	Tuple getImpl(std::index_sequence<Is...>);

	friend AsyncPostgres::Result;
};

class AsyncPostgres::Result::iterator
: public std::iterator<
	std::random_access_iterator_tag,
	AsyncPostgres::Result::Row,
	int,
	AsyncPostgres::Result::Row *,
	AsyncPostgres::Result::Row
> {
	PGresult * r;
	int rowIndex;

public:
	explicit iterator(PGresult *, int);
	iterator& operator++();
	iterator operator++(int);
	bool operator==(iterator) const;
	bool operator!=(iterator) const;
	reference operator*() const;
};

class AsyncPostgres::Notification {
	std::unique_ptr<PGnotify, void (*)(PGnotify *)> pgNotify;

	Notification(PGnotify *);

public:
	std::string_view channelName() const;
	int bePid() const;
	std::string_view extra() const;

	friend AsyncPostgres;
};

#include "AsyncPostgres.tpp"
