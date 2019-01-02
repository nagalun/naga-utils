#include <tuple>
#include <functional>
#include <iostream>

#include <tuple.hpp>

#include <uWS.h>

template<typename UserData>
template<typename PreProcessFunc>
PacketReader<UserData>::PacketReader(uWS::Hub& h, PreProcessFunc ppf) {
	h.onMessage([this, ppf{std::move(ppf)}] (uWS::WebSocket<uWS::SERVER> * ws, const char * msg, sz_t len, uWS::OpCode oc) {
		UserData * u = static_cast<UserData *>(ws->getUserData());
		if (u == nullptr || len == 0) {
			return;
		}

		auto search = handlers.find(OpCode(msg[0]));
		if (oc != uWS::OpCode::BINARY || search == handlers.end()) {
			ws->close(1003);
			return;
		}

		ppf(*u);

		try {
			search->second(*u, reinterpret_cast<const u8 *>(msg + 1), len - 1);
		} catch (const std::length_error& e) {
			std::cout << "Exception on packet read: " << e.what() << std::endl;
			ws->close(1002);
		}
	});
}

template<typename UserData>
template<typename Packet, typename Func>
void PacketReader<UserData>::on(Func f) {
	handlers.emplace(Packet::code, [f{std::move(f)}] (UserData& u, const u8 * data, sz_t size) {
		multiApply(f, Packet::fromBuffer(data, size), u);
	});
}
