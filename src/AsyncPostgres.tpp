#include <type_traits>
#include <stdexcept>
#include <optional>
#include <cstring>

#include <byteswap.hpp>
#include <stringparser.hpp>
#include <templateutils.hpp>
#include <explints.hpp>

namespace detail {

template <typename T>
using fromString_t = decltype(std::declval<T>().fromString("", 0));

template <typename T>
using has_fromString = detect<T, fromString_t>;

template <typename T>
using data_t = decltype(std::declval<T>().data());

template <typename T>
using has_data = detect<T, data_t>;

template <typename T>
using dataSizeBytes_t = decltype(std::declval<T>().dataSizeBytes());

template <typename T>
using has_dataSizeBytes = detect<T, dataSizeBytes_t>;


template<typename T>
typename std::enable_if<is_optional<T>::value, T>::type
getValue(char * buf, sz_t size) {
	if (!buf) {
		return std::nullopt;
	}

	using Tv = typename T::value_type;
	if constexpr (has_fromString<Tv>::value) {
		return Tv::fromString(buf, size);
	} else {
		return fromString<Tv>(std::string_view(buf, size));
	}
}

template<typename T>
typename std::enable_if<!is_optional<T>::value, T>::type
getValue(char * buf, sz_t size) {
	if (!buf) {
		throw std::invalid_argument("Value was null on non-nullable result");
	}

	if constexpr (has_fromString<T>::value) {
		return T::fromString(buf, size);
	} else {
		return fromString<T>(std::string_view(buf, size));
	}
}

template<typename T>
typename std::enable_if<!std::is_null_pointer<T>::value
		&& !has_const_iterator<T>::value
		&& !is_optional<T>::value, const char *>::type
getDataPointer(const T& value) {
	if constexpr (has_data<T>::value && has_dataSizeBytes<T>::value) {
		return value.data();
	} else {
		static_assert(!std::is_class<T>::value,
			"Class types must implement .data() and .dataSizeBytes() to be used in queries");
		if constexpr (std::is_pointer<T>::value) {
			// get pointer to the first element of the pointer, not the temporary variable
			return reinterpret_cast<const char *>(&value[0]);
		} else {
			return reinterpret_cast<const char *>(&value);
		}
	}
}

template<typename T>
typename std::enable_if<has_const_iterator<T>::value, const char *>::type
getDataPointer(const T& value) {
	return reinterpret_cast<const char *>(value.data());
}

template<typename T>
typename std::enable_if<std::is_null_pointer<T>::value
		|| std::is_same<T, std::nullopt_t>::value, const char *>::type
getDataPointer(const T&) {
	return nullptr;
}

template<sz_t N>
const char * getDataPointer(const char(& arr)[N]) {
	return &arr[0];
}

template<typename T>
typename std::enable_if<is_optional<T>::value, const char *>::type
getDataPointer(const T& value) {
	return value.has_value() ? getDataPointer(*value) : nullptr;
}

template<typename T>
typename std::enable_if<!has_const_iterator<T>::value
		&& !is_optional<T>::value, int>::type
getSize(const T& value) {
	if constexpr (has_data<T>::value && has_dataSizeBytes<T>::value) {
		return value.dataSizeBytes();
	} else if constexpr (std::is_pointer<T>::value) {
		static_assert(std::is_same<T, const char *>::value,
			"Use std::array or std::vector to pass non-text arrays!");
		return strlen(value); // :(
	} else {
		return sizeof(T);
	}
}

template<typename T>
typename std::enable_if<has_const_iterator<T>::value, int>::type
getSize(const T& value) {
	return value.size() * sizeof(typename T::value_type);
}

template<typename T>
typename std::enable_if<std::is_same<T, std::nullopt_t>::value, int>::type
getSize(const T&) {
	return 0;
}

/*template<sz_t N>
int getSize(const char(& arr)[N]) {
	return N - 1; // trim null terminator
}*/

template<typename T>
typename std::enable_if<is_optional<T>::value, int>::type
getSize(const T& value) {
	return value.has_value() ? getSize(*value) : 0;
}

template<typename T>
typename std::enable_if<std::is_arithmetic<T>::value
/*		&& !std::is_array<T>::value
		&& !is_std_array<T>::value
		&& !std::is_null_pointer<T>::value*/, void>::type
byteSwap(T& value) {
	switch (sizeof(T)) {
		case 8:
			value = bswap_64(value);
			break;

		case 4:
			value = bswap_32(value);
			break;

		case 2:
			value = bswap_16(value);
			break;
	}
}

template<typename T>
typename std::enable_if<!std::is_arithmetic<T>::value
/*		|| std::is_array<T>::value
		|| is_std_array<T>::value
		|| std::is_null_pointer<T>::value)*/
		&& !is_optional<T>::value, void>::type
byteSwap(T&) { }

template<typename T>
typename std::enable_if<is_optional<T>::value, void>::type
byteSwap(T& value) {
	if (value) {
		byteSwap(*value);
	}
}

}

template<typename... Ts>
template<std::size_t... Is>
AsyncPostgres::TemplatedQuery<Ts...>::TemplatedQuery(std::index_sequence<Is...>, int prio, std::string cmd, Ts&&... params)
: Query(prio, std::move(cmd), realValues, realLengths, realFormats, sizeof... (Ts)),
  valueStorage(std::forward<Ts>(params)...),
  realValues{detail::getDataPointer(std::get<Is>(valueStorage))...},
  realLengths{detail::getSize(std::get<Is>(valueStorage))...},
  realFormats{(Is, 1)...} { // set as many ones as there are type indexes
	(detail::byteSwap(std::get<Is>(valueStorage)), ...);
}

template<typename... Ts>
AsyncPostgres::TemplatedQuery<Ts...>::TemplatedQuery(int prio, std::string cmd, Ts&&... params)
: TemplatedQuery(std::index_sequence_for<Ts...>{}, prio, std::move(cmd), std::forward<Ts>(params)...) { }

template<int priority, typename... Ts>
ll::shared_ptr<AsyncPostgres::Query> AsyncPostgres::query(std::string command, Ts&&... params) {
	if (!busy && isConnected()) {
		signalCompletion();
	}

	// dereference the iterator returned by emplace, and the unique_ptr
	auto it = queries.emplace(ll::make_shared<AsyncPostgres::TemplatedQuery<Ts...>>(priority, std::move(command), std::forward<Ts>(params)...));
	(*it)->setQueueIterator(it);
	return *it;
}

template<typename Func, typename Tuple>
void AsyncPostgres::Result::forEach(Func f) {
	for (Row r : *this) {
		multiApply(f, r.getImpl<Tuple>(std::make_index_sequence<std::tuple_size<Tuple>::value>{}));
	}
}

template<typename Tuple, std::size_t... Is>
Tuple AsyncPostgres::Result::Row::getImpl(std::index_sequence<Is...>) {
	try {
		return {detail::getValue<typename std::tuple_element<Is, Tuple>::type>(
			PQgetisnull(r, rowIndex, Is)
			? nullptr
			: PQgetvalue(r, rowIndex, Is),
			PQgetlength(r, rowIndex, Is)
		)...};
	} catch (const std::exception& e) {
		throw ParseException({typeid(typename std::tuple_element<Is, Tuple>::type)...}, typeid(e), e.what());
	}
}

template<typename... Ts>
std::tuple<Ts...> AsyncPostgres::Result::Row::get() {
	return getImpl<std::tuple<Ts...>>(std::index_sequence_for<Ts...>{});
}

template<typename... Ts>
AsyncPostgres::Result::Row::operator std::tuple<Ts...>() {
	return getImpl<std::tuple<Ts...>>(std::index_sequence_for<Ts...>{});
}
