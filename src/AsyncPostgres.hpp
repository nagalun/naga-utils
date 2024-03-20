#pragma once

#include <coroutine>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <tuple>
#include <iterator>
#include <functional>
#include <set>
#include <type_traits>
#include <typeindex>
#include <stop_token>

#include "TimedCallbacks.hpp"
#include "explints.hpp"
#include "tuple.hpp"
#include "shared_ptr_ll.hpp"
#include "Poll.hpp"
#include "async.hpp"

#include <postgresql/libpq-fe.h>

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
	nev::Loop& loop;
	TimedCallbacks& tc;
	TimedCallbacks::TimerToken reconnectTimer;
	TimedCallbacks::TimerToken qRetryTimer;

	std::unique_ptr<nev::Async> nextCommandCaller;
	std::unique_ptr<PGconn, void (*)(PGconn *)> pgConn;
	std::unique_ptr<nev::Poll> pSock;
	QueryQueue queries;
	QueryQueue::const_iterator currentQuery; // queries.begin() could change at any time

	std::function<void(Notification)> notifFunc;
	std::function<void(ConnStatusType)> connChangeFunc;
	bool stopOnceEmpty;
	bool busy;
	bool awaitingResponse;
	bool autoReconnect;

public:
	AsyncPostgres(nev::Loop&, TimedCallbacks&);

	void connect(std::unordered_map<std::string, std::string> connParams = {}, bool expandDbname = false);
	bool reconnect(); // reconnects with the same parameters

	void lazyDisconnect(); // will disconnect when the query queue is empty
	void disconnect();

	void setAutoReconnect(bool);

	template<typename... Ts>
	ll::shared_ptr<Query> query(int priority, std::stop_token, std::string, Ts&&...);

	template<typename... Ts>
	ll::shared_ptr<Query> query(int priority, std::string, Ts&&...);

	template<typename... Ts>
	ll::shared_ptr<Query> query(std::stop_token, std::string, Ts&&...);

	template<typename... Ts>
	ll::shared_ptr<Query> query(std::string, Ts&&...);

	template<typename... Ts>
	ll::shared_ptr<Query> query(const char*, Ts&&...);

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

	template<PostgresPollingStatusType(*PollFunc)(PGconn *)>
	void pollConnection();

	void signalCompletion();
	void processNextCommand();
	void currentCommandReturnedResult(PGresult *, bool);
	void manageSocketEvents(bool);

	std::string_view getLastErrorFirstLine();
	void printLastError();
	void throwLastError();

	void socketCallback(nev::Poll&, int, int);
};

class AsyncPostgres::Result {
public:
	class Error;
	class Row;
	class iterator;

private:
	std::unique_ptr<PGresult, void (*)(PGresult *)> pgResult;
	PGconn * conn;

public:
	Result();
	Result(PGresult *, PGconn *);

	sz_t numAffected() const;
	sz_t size() const;
	sz_t rowSize() const;
	bool success() const;
	bool expectMoreResults() const;
	ExecStatusType getStatus() const;
	const char* getErrorMessage() const;
	[[noreturn]] void throwStatus() const;
	bool canCopyTo() const;

	bool blockingCopy(const char *, sz_t);
	bool blockingCopyEnd(const char * err = nullptr);

	iterator begin();
	iterator end();

	template<typename Func, typename Tuple = typename lambdaToTuple<Func>::type>
	void forEach(Func);

	Row operator[](int);
	operator bool() const;
};

class AsyncPostgres::Result::Error : std::exception {
	ExecStatusType errCode;
	std::string errMsg;

public:
	Error(ExecStatusType errCode, std::string errMsg);

	ExecStatusType code() const noexcept;
	const char* codeStr() const noexcept;
	const char* what() const noexcept override;
};

class AsyncPostgres::Result::Row {
public:
	class ParseException;

private:
	PGresult * r;
	const int rowIndex;

public:
	Row(PGresult *, int);

	template<typename... Ts>
	std::tuple<Ts...> get();

	template<typename... Ts>
	operator std::tuple<Ts...>();

	int getRow();

private:
	template<typename Tuple, std::size_t... Is>
	Tuple getImpl(std::index_sequence<Is...>);

	friend AsyncPostgres::Result;
};

class AsyncPostgres::Result::Row::ParseException : public std::exception {
	std::string message;

public:
	const std::vector<std::type_index> triedTypes;
	const std::type_index causingExceptionType;
	const std::string causingWhat;

	ParseException(std::vector<std::type_index>, std::type_index, std::string);

	virtual const char * what() const noexcept;
};

class AsyncPostgres::Result::iterator {
	PGresult * r;
	int rowIndex;

public:
	using iterator_category = std::random_access_iterator_tag;
	using value_type = AsyncPostgres::Result::Row;
	using difference_type = int;
	using pointer = value_type*;
	using reference = value_type;

	explicit iterator(PGresult *, int);
	iterator& operator++();
	iterator operator++(int);
	iterator& operator--();
	iterator operator--(int);
	int operator-(iterator);
	bool operator>(iterator) const;
	bool operator<(iterator) const;
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

class AsyncPostgres::Query {
	AsyncPostgres& ap;
	std::stop_callback<std::function<void(void)>> stopCb;
	std::string command;
	std::function<void(Result)> onDone;
	std::coroutine_handle<> coro;
	const char * const * values;
	const int * lengths;
	const int * formats;
	AsyncPostgres::QueryQueue::const_iterator queue_it;
	int nParams;
	const int priority;
	Result res;
	bool expectsResults;
	bool cancelled;

public:
	Query(AsyncPostgres&, int prio, std::stop_token, std::string, const char * const *, const int *, const int *, int);
	virtual ~Query();

	void markCancelled();

	bool isDone() const;
	void then(std::function<void(Result)>);

	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<> h);
	Result await_resume();

	bool operator>(const Query&) const;
	bool operator<(const Query&) const;

private:
	int send(PGconn *);
	void done(Result);
	AsyncPostgres::QueryQueue::const_iterator getQueueIterator() const;
	void setQueueIterator(AsyncPostgres::QueryQueue::const_iterator);

	friend AsyncPostgres;
};

AwaiterProxy<AsyncPostgres::Query> operator co_await(ll::shared_ptr<AsyncPostgres::Query>);

template<typename... Ts>
class AsyncPostgres::TemplatedQuery : public AsyncPostgres::Query {
	// we want to store the actual values, not references/pointers to them, so decay types
	std::tuple<typename std::decay<Ts>::type...> valueStorage;
	const std::array<const char *, sizeof... (Ts)> realValues;
	const std::array<int, sizeof... (Ts)> realLengths;
	const std::array<int, sizeof... (Ts)> realFormats;

	template<std::size_t... Is>
	TemplatedQuery(std::index_sequence<Is...>, AsyncPostgres&, int prio, std::stop_token, std::string, Ts&&...);

public:
	TemplatedQuery(AsyncPostgres&, int prio, std::stop_token, std::string, Ts&&...);
};

#include "AsyncPostgres.tpp" // IWYU pragma: keep
