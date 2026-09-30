#include "LyncSerial.h"

#include <string.h>

LyncSerial::LyncSerial(Stream& serial, uint8_t maxZones)
	: _serial(serial), _maxZones(maxZones) {
	if (_maxZones == 0 || _maxZones > 12) {
		_maxZones = 12;
	}
}

void LyncSerial::setZoneLimit(uint8_t maxZones) {
	if (maxZones == 0 || maxZones > 12) {
		return;
	}
	_maxZones = maxZones;
}

uint8_t LyncSerial::zoneLimit() const {
	return _maxZones;
}

bool LyncSerial::validateZone(uint8_t zone, bool allowBroadcast) const {
	if (allowBroadcast && zone == 0) {
		return true;
	}
	return zone >= 1 && zone <= _maxZones;
}

uint8_t LyncSerial::checksum(const uint8_t* bytes, size_t lenWithoutChecksum) {
	uint8_t sum = 0;
	for (size_t i = 0; i < lenWithoutChecksum; ++i) {
		sum = static_cast<uint8_t>(sum + bytes[i]);
	}
	return sum;
}

bool LyncSerial::sendRawFrame(uint8_t zone, uint8_t command, const uint8_t* data, size_t dataLen) {
	if (!validateZone(zone, true) || dataLen > (kMaxTxFrame - 5)) {
		return false;
	}

	uint8_t frame[kMaxTxFrame] = {0};
	frame[0] = kHead;
	frame[1] = kReserved;
	frame[2] = zone;
	frame[3] = command;

	for (size_t i = 0; i < dataLen; ++i) {
		frame[4 + i] = data[i];
	}

	const size_t lenNoChecksum = 4 + dataLen;
	frame[lenNoChecksum] = checksum(frame, lenNoChecksum);
	const size_t totalLen = lenNoChecksum + 1;

	const size_t written = _serial.write(frame, totalLen);
	_serial.flush();
	return written == totalLen;
}

bool LyncSerial::sendSingleByteCommand(uint8_t zone, TxCommand command, uint8_t data) {
	return sendRawFrame(zone, static_cast<uint8_t>(command), &data, 1);
}

bool LyncSerial::sendNameCommand(uint8_t zone, TxCommand command, uint8_t firstData, const char* name) {
	if (!validateZone(zone, false) || name == nullptr) {
		return false;
	}

	uint8_t payload[13] = {0};
	payload[0] = firstData;

	for (size_t i = 0; i < 10; ++i) {
		if (name[i] == '\0') {
			break;
		}
		payload[1 + i] = static_cast<uint8_t>(name[i]);
	}

	payload[12] = 0x00;
	return sendRawFrame(zone, static_cast<uint8_t>(command), payload, sizeof(payload));
}

bool LyncSerial::setMp3Repeat(bool enabled) {
	return sendSingleByteCommand(0x00, TxCommand::Mp3Repeat, enabled ? 0xFF : 0x00);
}

bool LyncSerial::mp3FastForward() {
	return sendSingleByteCommand(0x00, TxCommand::Common, 0x0A);
}

bool LyncSerial::mp3PlayPause() {
	return sendSingleByteCommand(0x00, TxCommand::Common, 0x0B);
}

bool LyncSerial::mp3FastBackward() {
	return sendSingleByteCommand(0x00, TxCommand::Common, 0x0C);
}

bool LyncSerial::mp3Stop() {
	return sendSingleByteCommand(0x00, TxCommand::Common, 0x0D);
}

bool LyncSerial::powerAll(bool on) {
	return sendSingleByteCommand(0x00, TxCommand::Common, on ? 0x55 : 0x56);
}

bool LyncSerial::powerZone(uint8_t zone, bool on) {
	if (!validateZone(zone, false)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::Common, on ? 0x57 : 0x58);
}

bool LyncSerial::muteZone(uint8_t zone, bool on) {
	if (!validateZone(zone, false)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::Common, on ? 0x1E : 0x1F);
}

bool LyncSerial::dndZone(uint8_t zone, bool on) {
	if (!validateZone(zone, false)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::Common, on ? 0x59 : 0x5A);
}

bool LyncSerial::selectInput(uint8_t zone, uint8_t input) {
	if (!validateZone(zone, false) || input < 1 || input > 18) {
		return false;
	}

	uint8_t data = 0;
	if (input <= 12) {
		data = static_cast<uint8_t>(0x0F + input);
	} else {
		data = static_cast<uint8_t>(0x56 + input);
	}

	return sendSingleByteCommand(zone, TxCommand::Common, data);
}

bool LyncSerial::selectPartyInput(uint8_t zone, uint8_t input) {
	if (!validateZone(zone, true) || input < 1 || input > 18) {
		return false;
	}

	uint8_t data = 0;
	if (input <= 12) {
		data = static_cast<uint8_t>(0x35 + input);
	} else {
		data = static_cast<uint8_t>(0x5C + input);
	}

	return sendSingleByteCommand(zone, TxCommand::Common, data);
}

bool LyncSerial::queryAllZoneStatus() {
	return sendSingleByteCommand(0x00, TxCommand::QueryAllZoneStatus, 0x01);
}

bool LyncSerial::setZoneName(uint8_t zone, const char* name) {
	return sendNameCommand(zone, TxCommand::ZoneNameSetting, 0x00, name);
}

bool LyncSerial::setSourceName(uint8_t zone, uint8_t source, const char* name) {
	if (source < 1 || source > 18) {
		return false;
	}
	return sendNameCommand(zone, TxCommand::SourceNameSetting, source, name);
}

bool LyncSerial::queryId() {
	return sendSingleByteCommand(0x00, TxCommand::QueryIdCode, 0x00);
}

bool LyncSerial::recallFile(uint8_t index) {
	if (index < 1 || index > 4) {
		return false;
	}
	return sendSingleByteCommand(0x00, TxCommand::RecallFile, index);
}

bool LyncSerial::saveFile(uint8_t index) {
	if (index < 1 || index > 4) {
		return false;
	}
	return sendSingleByteCommand(0x00, TxCommand::SaveFile, index);
}

bool LyncSerial::queryZoneStatus(uint8_t zone) {
	if (!validateZone(zone, false)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::QueryZoneStatus, 0x00);
}

bool LyncSerial::queryZoneName(uint8_t zone) {
	if (!validateZone(zone, false)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::QueryZoneName, 0x00);
}

bool LyncSerial::querySourceName(uint8_t zone, uint8_t source) {
	if (!validateZone(zone, false) || source < 1 || source > 18) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::QueryZoneSourceName, source);
}

uint8_t LyncSerial::encodeVolumeDb(uint8_t level) {
	return static_cast<uint8_t>(196 + level);
}

uint8_t LyncSerial::encodeToneOrBalanceCentered(int8_t value) {
	return static_cast<uint8_t>(0x80 + value);
}

uint8_t LyncSerial::encodeTrebleDb(int8_t db) {
	return static_cast<uint8_t>(db);
}

uint8_t LyncSerial::encodeBassDb(int8_t db) {
	return static_cast<uint8_t>(db);
}

int8_t LyncSerial::decodeSignedFromOffsetByte(uint8_t value) {
	return static_cast<int8_t>(value - 0x80);
}

int8_t LyncSerial::decodeSignedTwosComplement(uint8_t value) {
	return static_cast<int8_t>(value);
}

bool LyncSerial::setVolumeDb(uint8_t zone, uint8_t level) {
	if (!validateZone(zone, false) || level > 59) {
		return false;
	}
	if (!sendSingleByteCommand(zone, TxCommand::VolumeSetting, encodeVolumeDb(level))) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::Common, 0x1F);
}

bool LyncSerial::setBalanceDb(uint8_t zone, int8_t db) {
	if (!validateZone(zone, false) || db > 18 || db < -18) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::BalanceSetting, encodeToneOrBalanceCentered(db));
}

bool LyncSerial::setTrebleDb(uint8_t zone, int8_t db) {
	if (!validateZone(zone, false) || db > 10 || db < -10) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::TrebleSetting, encodeTrebleDb(db));
}

bool LyncSerial::setBassDb(uint8_t zone, int8_t db) {
	if (!validateZone(zone, false) || db > 10 || db < -10) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::BassSetting, encodeBassDb(db));
}

bool LyncSerial::setEchoMode(bool enabled) {
	return sendSingleByteCommand(0x00, TxCommand::SetEchoMode, enabled ? 0xFF : 0x00);
}

bool LyncSerial::setNamesToDefault() {
	return sendSingleByteCommand(0x00, TxCommand::SetNameToDefault, 0x00);
}

bool LyncSerial::setAudioToDefault(uint8_t zone) {
	if (!validateZone(zone, true)) {
		return false;
	}
	return sendSingleByteCommand(zone, TxCommand::SetAudioToDefault, 0x00);
}

LyncSerial::RxLengthKind LyncSerial::getRxLengthKind(uint8_t) const {
	return RxLengthKind::Unknown;
}

size_t LyncSerial::expectedFixedRxLength(uint8_t command) const {
	switch (command) {
	case 0x05:
	case 0x06:
	case 0x1B:
		return 14;
	case 0x09:
	case 0x13:
		return 6;
	case 0x0C:
	case 0x0D:
		return 17;
	case 0x14:
		return 22;
	default:
		return 0;
	}
}

bool LyncSerial::decodeZoneStatus(ParsedMessage& message) const {
	if (message.dataLen < 9) {
		return false;
	}

	const uint8_t d1 = message.data[0];
	const uint8_t d2 = message.data[1];
	const uint8_t d3 = message.data[2];

	message.zoneStatus.powerOn = (d1 & 0x01) != 0;
	message.zoneStatus.muteOn = (d1 & 0x02) != 0;
	message.zoneStatus.dndOn = (d1 & 0x04) != 0;

	message.zoneStatus.allOn = (d2 & 0x80) != 0;
	message.zoneStatus.allOff = (d2 & 0x40) != 0;
	message.zoneStatus.partyModeOn = (d2 & 0x20) != 0;

	message.zoneStatus.mp3RepeatOn = (d3 & 0x10) != 0;
	message.zoneStatus.partyModeInput = message.data[3];

	const uint8_t rawInput = message.data[4];
	if (rawInput <= 0x11) {
		message.zoneStatus.input = static_cast<uint8_t>(rawInput + 1);
	} else {
		message.zoneStatus.input = rawInput;
	}

	message.zoneStatus.volumeDb = decodeSignedTwosComplement(message.data[5]);
	message.zoneStatus.trebleDb = decodeSignedTwosComplement(message.data[6]);
	message.zoneStatus.bassDb = decodeSignedTwosComplement(message.data[7]);
	message.zoneStatus.balanceDb = decodeSignedTwosComplement(message.data[8]);
	return true;
}

bool LyncSerial::decodeAudioKeypadExist(ParsedMessage& message) const {
	if (message.dataLen < 9) {
		return false;
	}

	const uint8_t zoneExistLow = message.data[1];
	const uint8_t keypadExistLow = message.data[2];
	const uint8_t zoneExistHigh = message.data[3] & 0x0F;
	const uint8_t keypadExistHigh = message.data[4] & 0x0F;

	message.audioKeypadExist.zoneExistMask = static_cast<uint16_t>(zoneExistLow) |
											 static_cast<uint16_t>(zoneExistHigh << 8);
	message.audioKeypadExist.keypadExistMask = static_cast<uint16_t>(keypadExistLow) |
											   static_cast<uint16_t>(keypadExistHigh << 8);
	return true;
}

bool LyncSerial::decodeZoneSourceOrZoneName(ParsedMessage& message, bool) const {
	if (message.dataLen < 12) {
		return false;
	}

	memset(message.text, 0, sizeof(message.text));
	size_t out = 0;
	for (size_t i = 0; i < 11 && i < message.dataLen; ++i) {
		const char c = static_cast<char>(message.data[i]);
		if (c == '\0') {
			break;
		}
		message.text[out++] = c;
	}

	message.sourceOrZone = message.data[11];
	return true;
}

bool LyncSerial::decodeVariableText(ParsedMessage& message) const {
	memset(message.text, 0, sizeof(message.text));
	size_t out = 0;
	for (size_t i = 0; i < message.dataLen && out < kMaxTextLen; ++i) {
		const char c = static_cast<char>(message.data[i]);
		if (c == '\0') {
			break;
		}
		message.text[out++] = c;
	}
	return true;
}

bool LyncSerial::decodeRxFrame(ParsedMessage& message, const uint8_t* frame, size_t length) const {
	if (length < 6 || frame[0] != kHead || frame[1] != kReserved) {
		return false;
	}

	const uint8_t computed = checksum(frame, length - 1);
	if (computed != frame[length - 1]) {
		return false;
	}

	message = ParsedMessage{};
	message.zone = frame[2];
	message.command = frame[3];
	message.dataLen = length - 5;
	if (message.dataLen > sizeof(message.data)) {
		message.dataLen = sizeof(message.data);
	}
	memcpy(message.data, &frame[4], message.dataLen);

	switch (message.command) {
	case 0x05:
		message.type = RxType::ZoneStatus;
		return decodeZoneStatus(message);
	case 0x06:
		message.type = RxType::AudioKeypadExist;
		return decodeAudioKeypadExist(message);
	case 0x09:
		message.type = RxType::Mp3PlayEnd;
		return true;
	case 0x0C:
		message.type = RxType::ZoneSourceName;
		return decodeZoneSourceOrZoneName(message, true);
	case 0x0D:
		message.type = RxType::ZoneName;
		return decodeZoneSourceOrZoneName(message, false);
	case 0x11:
		message.type = RxType::Mp3FileName;
		return decodeVariableText(message);
	case 0x12:
		message.type = RxType::Mp3ArtistName;
		return decodeVariableText(message);
	case 0x13:
		message.type = RxType::Mp3On;
		return true;
	case 0x14:
		message.type = RxType::Mp3Off;
		return decodeVariableText(message);
	case 0x1B:
		message.type = RxType::ErrorStatus;
		message.errorCode = (message.dataLen > 0) ? message.data[0] : 0;
		return true;
	default:
		message.type = RxType::Unknown;
		return true;
	}
}

bool LyncSerial::poll(ParsedMessage& message) {
	while (_serial.available() > 0) {
		const int readValue = _serial.read();
		if (readValue < 0) {
			return false;
		}

		const uint8_t b = static_cast<uint8_t>(readValue);

		if (_rxIndex == 0 && b != kHead) {
			continue;
		}

		if (_rxIndex >= kMaxRxFrame) {
			_rxIndex = 0;
			_rxExpectedLength = 0;
			_rxVariable = false;
			continue;
		}

		_rxBuffer[_rxIndex++] = b;

		if (_rxIndex == 4) {
			const uint8_t cmd = _rxBuffer[3];
			if (cmd == 0x11 || cmd == 0x12) {
				_rxVariable = true;
				_rxExpectedLength = 0;
			} else {
				_rxVariable = false;
				_rxExpectedLength = expectedFixedRxLength(cmd);
				if (_rxExpectedLength == 0) {
					_rxIndex = 0;
					_rxExpectedLength = 0;
					_rxVariable = false;
					continue;
				}
			}
		}

		if (_rxVariable && _rxIndex >= 6) {
			for (size_t i = 4; i < _rxIndex; ++i) {
				if (_rxBuffer[i] == 0x00) {
					_rxExpectedLength = i + 2;
					break;
				}
			}
		}

		if (_rxExpectedLength > 0 && _rxIndex == _rxExpectedLength) {
			const bool ok = decodeRxFrame(message, _rxBuffer, _rxExpectedLength);
			_rxIndex = 0;
			_rxExpectedLength = 0;
			_rxVariable = false;
			return ok;
		}
	}

	return false;
}