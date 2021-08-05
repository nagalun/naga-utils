#include "Bucket.hpp"

Bucket::Bucket(Bucket::Rate rate, Bucket::Per per)
: rate(rate),
  per(per),
  allowance(rate) { }

Bucket::Bucket(Bucket::Rate rate, Bucket::Per per, Bucket::Allowance allowance)
: rate(rate),
  per(per),
  allowance(allowance) { }

void Bucket::set(Bucket::Rate nrate, Bucket::Per nper) {
	rate = nrate;
	per = nper < 1 ? 1 : nper;
	updateAllowance();
}

void Bucket::set(Bucket::Rate nrate, Bucket::Per nper, Bucket::Allowance nallowance) {
	rate = nrate;
	per = nper < 1 ? 1 : nper;
	allowance = nallowance;
}

bool Bucket::canSpend(Rate count) const {
	return count <= getAllowance();
}

bool Bucket::spend(Rate count, bool punishing) {
	updateAllowance();

	if (allowance < count) {
		if (punishing) {
			allowance = 0;
		}

		return false;
	}

	allowance -= count;
	return true;
}

Bucket::Rate Bucket::getRate() const {
	return rate;
}

Bucket::Per Bucket::getPer() const {
	return per;
}

Bucket::Allowance Bucket::getAllowance() const {
	const auto now = std::chrono::steady_clock::now();
	std::chrono::duration<Allowance> passed = now - lastCheck;
	Allowance ace = allowance + passed.count() * (static_cast<Allowance>(rate) / per);
	return ace > rate ? rate : ace;
}

Bucket::Allowance Bucket::updateAllowance() {
	const auto now = std::chrono::steady_clock::now();
	std::chrono::duration<Allowance> passed = now - lastCheck;
	Allowance ace = allowance + passed.count() * (static_cast<Allowance>(rate) / per);
	allowance = ace > rate ? rate : ace;
	lastCheck = now;
	return allowance;
}
