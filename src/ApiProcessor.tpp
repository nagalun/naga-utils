#include <nlohmann/json.hpp>
#include <stringparser.hpp>

template<typename Func>
void ApiProcessor::TemplatedEndpointBuilder::end(Func f) {
	targetClass.add(method, std::make_unique<ApiProcessor::OutsiderTemplatedEndpoint<Func>>(
		std::move(varMarkers), std::move(f)));
}

template<typename TTuple>
ApiProcessor::TemplatedEndpoint<TTuple>::TemplatedEndpoint(std::vector<std::string> path)
: pathSections(std::move(path)) {
	sz_t j = 0;
	for (sz_t i = 0; i < pathSections.size(); i++) {
		if (pathSections[i].size() == 0) {
			if (j == varPositions.size()) {
				throw std::runtime_error("Templated arg count > Path var count");
			}

			varPositions[j++] = i;
		}
	}

	if (j != varPositions.size()) {
		throw std::runtime_error("Templated arg count (" + std::to_string(varPositions.size()) + ") != Path var count (" + std::to_string(j) + ")");
	}
}

template<typename TTuple>
bool ApiProcessor::TemplatedEndpoint<TTuple>::verify(const std::vector<std::string>& args) {
	if (args.size() != pathSections.size()) {
		return false;
	}

	for (sz_t i = 0; i < pathSections.size(); i++) {
		if (pathSections[i].size() != 0 && pathSections[i] != args[i]) {
			return false;
		}
	}

	return true;
}

template<typename TTuple>
TTuple ApiProcessor::TemplatedEndpoint<TTuple>::getTuple(std::vector<std::string> args) {
	return getTupleImpl(std::move(args), std::make_index_sequence<std::tuple_size<TTuple>::value>{});
}

template<typename TTuple>
template<std::size_t... Is>
TTuple ApiProcessor::TemplatedEndpoint<TTuple>::getTupleImpl(std::vector<std::string> args, std::index_sequence<Is...>) {
	return TTuple{fromString<typename std::tuple_element<Is, TTuple>::type>(args[varPositions[Is]])...};
}

template<typename Func, typename TTuple>
ApiProcessor::OutsiderTemplatedEndpoint<Func, TTuple>::OutsiderTemplatedEndpoint(std::vector<std::string> path, Func f)
: TemplatedEndpoint<TTuple>(std::move(path)),
  outsiderHandler(std::move(f)) { }

template<typename Func, typename TTuple>
void ApiProcessor::OutsiderTemplatedEndpoint<Func, TTuple>::exec(ll::shared_ptr<Request> req, std::string_view body, std::vector<std::string> args) {
	multiApply(outsiderHandler, ApiProcessor::TemplatedEndpoint<TTuple>::getTuple(std::move(args)), std::move(req), body);
}
