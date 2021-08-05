#pragma once

#include <chrono>

#include <explints.hpp>

class Bucket {
public:
	using Rate = u16;
	using Per = u16;
	using Allowance = float;
	enum class Punishment {
		NONE,
		SET_TO_ZERO,
		ALLOW_NEGATIVE
	};

private:
	u16 rate;
	u16 per;
	Allowance allowance;
	std::chrono::steady_clock::time_point lastCheck;

public:
	Bucket(Rate, Per);
	Bucket(Rate, Per, Allowance);

	void set(Rate, Per);
	void set(Rate, Per, Allowance);

	bool canSpend(Rate = 1) const;
	bool spend(Rate = 1, Punishment = Punishment::NONE);

	Rate getRate() const;
	Per getPer() const;
	Allowance getAllowance() const;

private:
	Allowance updateAllowance();
};
