// The SD driver's reply decisions (assets/SdProtocol.h), which hal::SdSpi applies on the device.
#include <unity.h>

#include <cstring>

#include "assets/SdProtocol.h"

using namespace assets::sd;

void setUp() {}
void tearDown() {}

static void test_cmd8_reply() {
    TEST_ASSERT_TRUE(ifCond(0x01) == IfCond::V2);
    TEST_ASSERT_TRUE(ifCond(0x05) == IfCond::V1);        // idle + illegal command: SD 1.x
    TEST_ASSERT_TRUE(ifCond(0xFF) == IfCond::NoAnswer);  // not SD 1.x, though bit 2 is set
    TEST_ASSERT_TRUE(ifCond(0x00) == IfCond::NoAnswer);  // not idle after CMD0
    TEST_ASSERT_TRUE(ifCond(0x09) == IfCond::NoAnswer);  // CRC error
    TEST_ASSERT_TRUE(ifCond(0x04) == IfCond::NoAnswer);
}

static void test_read_failures_that_fall_back_to_10_mhz() {
    TEST_ASSERT_TRUE(cmd17(0x00) == Read::Ok);
    TEST_ASSERT_TRUE(cmd17(0xFF) == Read::Signal);
    TEST_ASSERT_TRUE(cmd17(0x20) == Read::Refused);  // address error
    TEST_ASSERT_TRUE(cmd17(0x40) == Read::Refused);  // parameter error
    TEST_ASSERT_TRUE(cmd17(0xF0) == Read::Signal);   // R1's top bit is always 0: noise, not a refusal
    TEST_ASSERT_TRUE(cmd17(0x80) == Read::Signal);
    TEST_ASSERT_TRUE(cmd24(0xF0) == Write::Signal);
    TEST_ASSERT_TRUE(cmd24(0x84) == Write::Signal);
    TEST_ASSERT_TRUE(cmd17(0x04) == Read::Refused);  // illegal command
    TEST_ASSERT_TRUE(retrySlower(Read::Signal));     // timeouts, error tokens and CRC mismatches too
    TEST_ASSERT_FALSE(retrySlower(Read::Refused));
    TEST_ASSERT_FALSE(retrySlower(Read::Ok));
}

static void test_command_crc7() {
    auto frame = [](uint8_t cmd, uint32_t arg) { return commandCrc(cmd, arg); };
    TEST_ASSERT_EQUAL_HEX8(0x95, frame(0, 0));        // the fixed CRCs CMD0 and CMD8 always need
    TEST_ASSERT_EQUAL_HEX8(0x87, frame(8, 0x1AA));
    TEST_ASSERT_EQUAL_HEX8(0x55, frame(17, 0));       // the SD spec's CMD17 example
    TEST_ASSERT_EQUAL_HEX8(0x01, frame(59, 0) & 0x01);  // the end bit is always set
    TEST_ASSERT_TRUE(frame(24, 0x1000) != frame(24, 0x1001));  // one address bit changes it
    TEST_ASSERT_TRUE(frame(24, 0x1000) != frame(17, 0x1000));
}

static void test_command_crc_error_is_a_signal_failure() {
    TEST_ASSERT_TRUE(cmd17(0x08) == Read::Signal);  // R1 bit 3: the card saw a bad command CRC
    TEST_ASSERT_TRUE(cmd17(0x09) == Read::Signal);
    TEST_ASSERT_TRUE(cmd17(0x20) == Read::Refused);
    TEST_ASSERT_TRUE(cmd24(0x00) == Write::Ok);
    TEST_ASSERT_TRUE(cmd24(0xFF) == Write::Signal);
    TEST_ASSERT_TRUE(cmd24(0x08) == Write::Signal);
    TEST_ASSERT_TRUE(cmd24(0x20) == Write::Refused);  // address error
    TEST_ASSERT_TRUE(cmd24(0x04) == Write::Refused);
}

static void test_data_crc() {
    uint8_t block[512];
    std::memset(block, 0xFF, sizeof block);
    TEST_ASSERT_EQUAL_HEX16(0x7FA1, dataCrc(block));  // the SD spec's example: 512 bytes of 0xFF
    std::memset(block, 0, sizeof block);
    TEST_ASSERT_EQUAL_HEX16(0x0000, dataCrc(block));
    block[100] = 1;
    TEST_ASSERT_NOT_EQUAL(0, dataCrc(block));
    for (int seed = 1; seed < 50; ++seed) {  // the nibble table agrees with the bitwise CRC
        for (int i = 0; i < 512; ++i) block[i] = uint8_t(i * seed + seed * seed);
        TEST_ASSERT_EQUAL_HEX16(assets::crc16(block, 512, 0), dataCrc(block));
    }
    TEST_ASSERT_EQUAL_HEX16(0x31C3, assets::crc16(reinterpret_cast<const uint8_t*>("123456789"), 9, 0));  // XMODEM
}

static void test_write_data_response() {
    TEST_ASSERT_TRUE(dataResponse(0xE5) == Write::Ok);       // xxx0 0101: accepted
    TEST_ASSERT_TRUE(dataResponse(0x05) == Write::Ok);       // the unused high bits are 0 on some cards
    TEST_ASSERT_TRUE(dataResponse(0xEB) == Write::Signal);   // 0 101 1: CRC error
    TEST_ASSERT_TRUE(dataResponse(0xED) == Write::Refused);  // 0 110 1: write error
    TEST_ASSERT_TRUE(dataResponse(0xFF) == Write::Signal);   // no answer
    TEST_ASSERT_TRUE(dataResponse(0x00) == Write::Signal);   // still busy: not a response
    TEST_ASSERT_TRUE(dataResponse(0xFE) == Write::Signal);   // a start token, not a response
}

static void test_timeouts() {
    TEST_ASSERT_EQUAL(2000, kIdleMs);  // ACMD41
    TEST_ASSERT_EQUAL(100, kTokenMs);
    TEST_ASSERT_EQUAL(300, kBusyMs);
    TEST_ASSERT_EQUAL(10000000, kSafeHz);
    TEST_ASSERT_EQUAL(500, kWriteBusyMs);
}

static void test_multi_block_transfers() {
    TEST_ASSERT_EQUAL_HEX8(0xFE, kStartBlock);
    TEST_ASSERT_EQUAL_HEX8(0xFC, kStartMultiWrite);
    TEST_ASSERT_EQUAL_HEX8(0xFD, kStopTran);
    // The commands answer as the single-block ones do.
    for (uint8_t r1 : {0x00, 0xFF, 0x20, 0x40, 0x08, 0xF0, 0x04}) {
        TEST_ASSERT_TRUE(cmd18(r1) == cmd17(r1));
        TEST_ASSERT_TRUE(cmd25(r1) == cmd24(r1));
    }
    TEST_ASSERT_TRUE(dataToken(0xFE) == Read::Ok);
    TEST_ASSERT_TRUE(dataToken(0xFF) == Read::Signal);  // the wait ran out
    TEST_ASSERT_TRUE(dataToken(0x08) == Read::Signal);  // an error token: retried slower, as for CMD17
    TEST_ASSERT_TRUE(dataToken(0xFC) == Read::Signal);
    // The stop: anything that is an R1 at all, a range error near the card's end included.
    TEST_ASSERT_TRUE(stopped(0x00));
    TEST_ASSERT_TRUE(stopped(0x40));
    TEST_ASSERT_FALSE(stopped(0xFF));
    TEST_ASSERT_FALSE(stopped(0x80));
    TEST_ASSERT_FALSE(stopped(0x08));  // a command CRC error: the CMD12 was ignored
    TEST_ASSERT_FALSE(stopped(0x48));
    TEST_ASSERT_TRUE(stopped(0x20));   // an address error is still a stop
    // CMD12's frame, sent without the usual busy wait, still carries its CRC.
    TEST_ASSERT_EQUAL_HEX8(0x61, commandCrc(12, 0));
}

// The clock steps down a rung at a time on a signal failure: 20 MHz, 10, then nothing slower.
static void test_clock_ladder() {
    TEST_ASSERT_EQUAL(10000000, slower(kFastHz));
    TEST_ASSERT_EQUAL(0, slower(kSafeHz));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cmd8_reply);
    RUN_TEST(test_read_failures_that_fall_back_to_10_mhz);
    RUN_TEST(test_command_crc7);
    RUN_TEST(test_command_crc_error_is_a_signal_failure);
    RUN_TEST(test_data_crc);
    RUN_TEST(test_write_data_response);
    RUN_TEST(test_timeouts);
    RUN_TEST(test_multi_block_transfers);
    RUN_TEST(test_clock_ladder);
    return UNITY_END();
}
