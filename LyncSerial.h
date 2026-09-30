#pragma once

#include <Arduino.h>

class LyncSerial {
public:
	static constexpr uint8_t kHead = 0x02;
	static constexpr uint8_t kReserved = 0x00;
	static constexpr size_t kMaxTxFrame = 32;
	static constexpr size_t kMaxRxFrame = 80;
	static constexpr size_t kMaxTextLen = 64;

	enum class RxType : uint8_t {
		None,
		ZoneStatus,
		AudioKeypadExist,
		Mp3PlayEnd,
		ZoneSourceName,
		ZoneName,
		Mp3FileName,
		Mp3ArtistName,
		Mp3On,
		Mp3Off,
		ErrorStatus,
		Unknown
	};

	enum class TxCommand : uint8_t {
		Mp3Repeat = 0x01,
		Common = 0x04,
		QueryAllZoneStatus = 0x05,
		ZoneNameSetting = 0x06,
		SourceNameSetting = 0x07,
		QueryIdCode = 0x08,
		RecallFile = 0x0A,
		SaveFile = 0x0B,
		QueryZoneStatus = 0x0C,
		QueryZoneName = 0x0D,
		QueryZoneSourceName = 0x0E,
		VolumeSetting = 0x15,
		BalanceSetting = 0x16,
		TrebleSetting = 0x17,
		BassSetting = 0x18,
		SetEchoMode = 0x19,
		SetNameToDefault = 0x1C,
		SetAudioToDefault = 0x1E
	};

	struct ZoneStatus {
		bool powerOn = false;
		bool muteOn = false;
		bool dndOn = false;
		bool allOn = false;
		bool allOff = false;
		bool partyModeOn = false;
		bool mp3RepeatOn = false;
		uint8_t partyModeInput = 0;
		uint8_t input = 0;
		int8_t volumeDb = 0;
		int8_t trebleDb = 0;
		int8_t bassDb = 0;
		int8_t balanceDb = 0;
	};

	struct AudioKeypadExist {
		uint16_t zoneExistMask = 0;
		uint16_t keypadExistMask = 0;
	};

	struct ParsedMessage {
		RxType type = RxType::None;
		uint8_t zone = 0;
		uint8_t command = 0;
		uint8_t data[kMaxTextLen + 12] = {0};
		size_t dataLen = 0;

		ZoneStatus zoneStatus;
		AudioKeypadExist audioKeypadExist;

		char text[kMaxTextLen + 1] = {0};
		uint8_t sourceOrZone = 0;
		uint8_t errorCode = 0;
	};

	explicit LyncSerial(Stream& serial, uint8_t maxZones = 12);

	void setZoneLimit(uint8_t maxZones);
	uint8_t zoneLimit() const;

	bool setMp3Repeat(bool enabled);
	bool mp3FastForward();
	bool mp3PlayPause();
	bool mp3FastBackward();
	bool mp3Stop();

	bool powerAll(bool on);
	bool powerZone(uint8_t zone, bool on);
	bool muteZone(uint8_t zone, bool on);
	bool dndZone(uint8_t zone, bool on);

	bool selectInput(uint8_t zone, uint8_t input);
	bool selectPartyInput(uint8_t zone, uint8_t input);

	bool queryAllZoneStatus();
	bool setZoneName(uint8_t zone, const char* name);
	bool setSourceName(uint8_t zone, uint8_t source, const char* name);
	bool queryId();
	bool recallFile(uint8_t index);
	bool saveFile(uint8_t index);
	bool queryZoneStatus(uint8_t zone);
	bool queryZoneName(uint8_t zone);
	bool querySourceName(uint8_t zone, uint8_t source);

	bool setVolumeDb(uint8_t zone, uint8_t level);
	bool setBalanceDb(uint8_t zone, int8_t db);
	bool setTrebleDb(uint8_t zone, int8_t db);
	bool setBassDb(uint8_t zone, int8_t db);

	bool setEchoMode(bool enabled);
	bool setNamesToDefault();
	bool setAudioToDefault(uint8_t zone);

	bool sendRawFrame(uint8_t zone, uint8_t command, const uint8_t* data, size_t dataLen);

	bool poll(ParsedMessage& message);

private:
	enum class RxLengthKind : uint8_t {
		Fixed9,
		Fixed14,
		Fixed17,
		VariableNullTerminated,
		Unknown
	};

	Stream& _serial;
	uint8_t _maxZones;

	uint8_t _rxBuffer[kMaxRxFrame] = {0};
	size_t _rxIndex = 0;
	size_t _rxExpectedLength = 0;
	bool _rxVariable = false;

	bool validateZone(uint8_t zone, bool allowBroadcast) const;
	bool sendSingleByteCommand(uint8_t zone, TxCommand command, uint8_t data);
	bool sendNameCommand(uint8_t zone, TxCommand command, uint8_t firstData, const char* name);

	static uint8_t checksum(const uint8_t* bytes, size_t lenWithoutChecksum);
	static uint8_t encodeVolumeDb(uint8_t level);
	static uint8_t encodeToneOrBalanceCentered(int8_t value);
	static uint8_t encodeTrebleDb(int8_t db);
	static uint8_t encodeBassDb(int8_t db);
	static int8_t decodeSignedFromOffsetByte(uint8_t value);
	static int8_t decodeSignedTwosComplement(uint8_t value);

	RxLengthKind getRxLengthKind(uint8_t command) const;
	size_t expectedFixedRxLength(uint8_t command) const;
	bool decodeRxFrame(ParsedMessage& message, const uint8_t* frame, size_t length) const;

	bool decodeZoneStatus(ParsedMessage& message) const;
	bool decodeAudioKeypadExist(ParsedMessage& message) const;
	bool decodeZoneSourceOrZoneName(ParsedMessage& message, bool isSourceName) const;
	bool decodeVariableText(ParsedMessage& message) const;
};