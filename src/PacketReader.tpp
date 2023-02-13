#pragma once
#include "PacketReader.hpp"
#include "tuple.hpp"

template<typename... ExtraArgs>
bool PacketReader<ExtraArgs...>::read(ExtraArgs&&... args, const u8 * buf, sz_t size) {
	OpCode opc(buf[0]);
	auto search = handlers.find(opc);

	if (search != handlers.end()) {
		search->second(std::forward<ExtraArgs>(args)..., buf + 1, size - 1);
		return true;
	}

	return false;
}

template<typename... ExtraArgs>
template<typename Packet, typename Func>
void PacketReader<ExtraArgs...>::on(Func f) {
	handlers.emplace(Packet::code, [f{std::move(f)}] (ExtraArgs&&... args, const u8 * data, sz_t size) {
		//std::fputs("Parsing opc ", stdout);
		//std::puts(std::to_string(u16(Packet::code)).c_str());

		multiApply(f, Packet::fromBuffer(data, size), std::forward<ExtraArgs>(args)...);
	});
}
