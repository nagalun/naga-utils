#include "PropertyReader.hpp"

#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <fstream>

// TODO: don't allow spaces in key names, or escape them

static std::string parseSpecial(std::string str) {
	std::size_t i = 0;
	while ((i = str.find('\\', i)) != std::string::npos) {
		i++;

		if (i == str.size()) {
			str.erase(i - 1);
			break;
		}

		switch (str[i]) {
			case 'n':
				str[i - 1] = '\n';
				break;

			default:
				str[i - 1] = str[i];
				break;
		}

		str.erase(i, 1);
	}

	return str;
}

static std::string serializeSpecial(std::string str) {
	std::size_t i = 0;
	while ((i = str.find_first_of("\n\\", i)) != std::string::npos) {
		switch (str[i]) {
			case '\n':
				str[i] = 'n';
				[[fallthrough]];
			case '\\':
				str.insert(str.begin() + i, '\\');
				i += 2;
				break;
		}
	}

	return str;
}

PropertyReader::PropertyReader(std::string_view path)
: filePath(path),
  propsChanged(false) {
	readFromDisk();
}

PropertyReader::PropertyReader(PropertyReader&& p)
: filePath(std::move(p.filePath)),
  propsChanged(p.propsChanged),
  props(std::move(p.props)) {
  	// prevent the moved class from saving to the file on destruction
	p.propsChanged = false;
}

PropertyReader::~PropertyReader() {
	writeToDisk();
}

/* Returns false if it couldn't open the file */
bool PropertyReader::readFromDisk() {
	std::string prop;
	std::ifstream file(filePath);
	bool ok = !!file;
	while (file.good()) {
		std::getline(file, prop);
		if (prop.size() > 0) {
			std::size_t keylen = prop.find_first_of(' ');
			if (keylen != std::string::npos) {
				props.insert_or_assign(parseSpecial(prop.substr(0, keylen)), parseSpecial(prop.substr(keylen + 1)));
			}
		}

		prop.clear();
	}

	return ok;
}

bool PropertyReader::writeToDisk(bool force) {
	if (!propsChanged && !force) {
		return false;
	}

	if (props.size() != 0) {
		std::ofstream file(filePath, std::ios_base::trunc);
		// just throw if it couldn't open the file
		for (auto & kv : props) {
			file << serializeSpecial(kv.first) << ' ' << serializeSpecial(kv.second) << '\n';
		}
	} else if (std::remove(filePath.c_str())) {
		int err = errno;
		throw std::runtime_error("Couldn't delete empty file (" + filePath + "): " + std::strerror(err));
	}

	return true;
}

bool PropertyReader::isEmpty() const {
	return props.empty();
}

bool PropertyReader::hasProp(std::string_view key) const {
	return props.find(key) != props.end();
}

std::string_view PropertyReader::getProp(std::string_view key, std::string_view defval) const {
	auto search = props.find(key);
	if (search != props.end()) {
		return search->second;
	}

	return defval;
}

std::string_view PropertyReader::getOrSetProp(std::string_view key, std::string_view defval) {
	auto search = props.find(key);
	if (search == props.end()) {
		setProp(key, std::string(defval));
		return defval;
	}

	return search->second;
}

void PropertyReader::setProp(std::string_view key, std::string value) {
	if (!value.size()) {
		propsChanged |= delProp(key);
	} else {
		props.insert_or_assign(std::string(key), std::move(value));
		propsChanged = true;
	}
}

bool PropertyReader::delProp(std::string_view key) {
	auto search = props.find(key);
	if (search != props.end()) {
		props.erase(search);
		return true;
	}

	return false;
}

PropertyReader PropertyReader::copy() {
	return PropertyReader(*this);
}

void PropertyReader::markAsWritten() {
	propsChanged = false;
}

bool PropertyReader::isFileOutdated() const {
	return propsChanged;
}
