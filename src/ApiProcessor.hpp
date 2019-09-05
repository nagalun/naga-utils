#pragma once

#include <functional>
#include <utility>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <string>

#include <Ip.hpp>
#include <explints.hpp>
#include <fwd_uWS.h>
#include <shared_ptr_ll.hpp>
#include <HttpData.hpp>
#include <tuple.hpp>

#include <nlohmann/json_fwd.hpp>

class Request;

class ApiProcessor {
public:
	class Endpoint;
	class TemplatedEndpointBuilder;

	template<typename Tuple>
	class TemplatedEndpoint;

	template<typename Func, typename Tuple = typename sliceTuple<typename lambdaToTuple<Func>::type, 2>::type>
	class OutsiderTemplatedEndpoint;

	enum Method : u8 {
		MGET,
		MPOST,
		MPUT,
		MDELETE,
		MPATCH,
		MOPTIONS,
		MHEAD,
		MTRACE,
		MCONNECT,
		MINVALID
	};

private:
	/* one vector for each method, except invalid */
	std::array<std::vector<std::unique_ptr<Endpoint>>, 9> definedEndpoints;

public:
	ApiProcessor(uWS::Hub&);

	TemplatedEndpointBuilder on(Method);
	void add(Method, std::unique_ptr<Endpoint>);

	// splits form parameters from a request body
	static std::map<std::string_view, std::string_view, std::less<>> getPostParameters(std::string_view);

private:
	void exec(ll::shared_ptr<Request>, std::string_view, std::vector<std::string>);
};

class Request : public HttpData {
	std::function<void(ll::shared_ptr<Request>)> cancelHandler;
	std::string bufferedData;

	uWS::HttpResponse * res;
	Ip ip;
	bool isProxied; // aka request went through nginx
	bool ended;

public:
	Request(uWS::HttpResponse *, uWS::HttpRequest *);

	Ip getIp() const;
	uWS::HttpResponse * getResponse();

	void setCookie(std::string_view key, std::string_view value, std::vector<std::string_view> directives = {});
	void delCookie(std::string_view key, std::vector<std::string_view> directives = {});

	void clearBufferedData();
	void writeStatus(std::string_view);
	void writeHeader(std::string_view key, std::string_view val);
	void end(const char *, sz_t);
	void end(nlohmann::json);
	void end();

	bool isCancelled() const;
	bool isCompleted() const;
	void onCancel(std::function<void(ll::shared_ptr<Request>)>);

private:
	void write(const char *, sz_t);
	void writeAndEnd(const char *, sz_t);

	void cancel(ll::shared_ptr<Request>);
	void updateData(uWS::HttpResponse *, uWS::HttpRequest *);
	void invalidateData();
	void maybeUpdateIp();

	friend ApiProcessor;
};

class ApiProcessor::TemplatedEndpointBuilder {
	ApiProcessor& targetClass;

	std::vector<std::string> varMarkers;
	Method method;

	TemplatedEndpointBuilder(ApiProcessor&, Method);

public:
	TemplatedEndpointBuilder& path(std::string);
	TemplatedEndpointBuilder& var();

	template<typename Func>
	void end(Func);

	friend ApiProcessor;
};

class ApiProcessor::Endpoint {
public:
	Endpoint();
	virtual ~Endpoint();

	virtual bool verify(const std::vector<std::string>&) = 0;
	virtual void exec(ll::shared_ptr<Request>, std::string_view, std::vector<std::string>);
};

template<typename TTuple>
class ApiProcessor::TemplatedEndpoint : public ApiProcessor::Endpoint {
public:
	using Tuple = TTuple;

private:
	const std::vector<std::string> pathSections; // empty str = variable placeholder
	std::array<u8, std::tuple_size<TTuple>::value> varPositions; // indexes of variables in path

public:
	TemplatedEndpoint(std::vector<std::string>);

	bool verify(const std::vector<std::string>&);

protected:
	TTuple getTuple(std::vector<std::string>);

private:
	template<std::size_t... Is>
	TTuple getTupleImpl(std::vector<std::string>, std::index_sequence<Is...>);
};

template<typename Func, typename TTuple>
class ApiProcessor::OutsiderTemplatedEndpoint
: public virtual TemplatedEndpoint<TTuple> {
	Func outsiderHandler;

public:
	OutsiderTemplatedEndpoint(std::vector<std::string>, Func);

	void exec(ll::shared_ptr<Request>, std::string_view, std::vector<std::string>);
};

#include "ApiProcessor.tpp"
