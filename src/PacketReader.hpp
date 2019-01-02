#pragma once

#include <unordered_map>
#include <functional>

#include <explints.hpp>
#include <fwd_uWS.h>

template<typename UserData>
class PacketReader {
	using OpCode = u8;
	std::unordered_map<OpCode, std::function<void(UserData&, const u8 *, sz_t)>> handlers;

public:
	template<typename PreProcessFunc>
	PacketReader(uWS::Hub&, PreProcessFunc);

	template<typename Packet, typename Func>
	void on(Func);
};

#include "PacketReader.tpp"
