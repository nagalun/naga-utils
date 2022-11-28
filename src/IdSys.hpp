#pragma once

#include <set>
#include <cstdint>

template<typename N = std::uint32_t>
class IdSys {
	N currentId;
	std::set<N> freeIds;

public:
	IdSys();

	N peekNextId() const;
	N getId();
	void freeId(N);

private:
	void shrink();
};
