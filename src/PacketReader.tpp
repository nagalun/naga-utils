#include <tuple>
#include <functional>
#include <iostream>

#include <misc/tuple.hpp>

#include <uWS.h>

template<typename UserData>
template<typename PreProcessFunc>
PacketReader<UserData>::PacketReader(uWS::Hub& h, PreProcessFunc ppf) {
	h.onMessage([this, ppf{std::move(ppf)}] (uWS::WebSocket<uWS::SERVER> * ws, const char * msg, sz_t len, uWS::OpCode oc) {
		Client * cl = static_cast<Client *>(ws->getUserData());
		if (cl == nullptr || len == 0) {
			return;
		}

		auto search = handlers.find(OpCode(msg[0]));
		if (oc != uWS::OpCode::BINARY || search == handlers.end()) {
			ws->close(1003);
			return;
		}

		ppf(*cl);

		try {
			search->second(*cl, reinterpret_cast<const u8 *>(msg + 1), len - 1);
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
