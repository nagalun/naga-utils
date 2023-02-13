#pragma once

#include "explints.hpp"
#include <unordered_map>
#include <functional>

template<typename... ExtraArgs>
class PacketReader {
	using OpCode = u8;
	std::unordered_map<OpCode, std::function<void(ExtraArgs&&..., const u8 *, sz_t)>> handlers;

public:
	bool read(ExtraArgs&&..., const u8 *, sz_t);

	template<typename Packet, typename Func>
	void on(Func);
};

#include "PacketReader.tpp" // IWYU pragma: keep
