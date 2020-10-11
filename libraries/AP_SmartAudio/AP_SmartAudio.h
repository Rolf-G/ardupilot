#pragma once

#include <AP_HAL/AP_HAL.h>


#ifndef HAL_SMARTAUDIO_ENABLED
#define HAL_SMARTAUDIO_ENABLED !HAL_MINIMIZE_FEATURES
#endif

#if CONFIG_HAL_BOARD == HAL_BOARD_SITL
#define BOARD_IS_SITL
#endif

#if HAL_SMARTAUDIO_ENABLED

#include <AP_Param/AP_Param.h>
#include <AP_SerialManager/AP_SerialManager.h>
#include <AP_HAL/utility/RingBuffer.h>
#include <AP_RCTelemetry/AP_VideoTX.h>

#define SMARTAUDIO_BUFFER_CAPACITY 5

// SmartAudio Serial Protocol
#define AP_SMARTAUDIO_UART_BAUD            4860
#define AP_SMARTAUDIO_SMARTBAUD_MIN        4560
#define AP_SMARTAUDIO_SMARTBAUD_MAX        5040
#define AP_SMARTAUDIO_SMARTBAUD_STEP       60
#define AP_SMARTAUDIO_UART_BUFSIZE_RX      16
#define AP_SMARTAUDIO_UART_BUFSIZE_TX      16
#define AP_SMARTAUDIO_MAX_PACKET_SIZE      32

#define SMARTAUDIO_SYNC_BYTE            0xAA
#define SMARTAUDIO_HEADER_BYTE          0x55
#define SMARTAUDIO_START_CODE           SMARTAUDIO_SYNC_BYTE + SMARTAUDIO_HEADER_BYTE
#define SMARTAUDIO_GET_PITMODE_FREQ     (1 << 14)
#define SMARTAUDIO_SET_PITMODE_FREQ     (1 << 15)
#define SMARTAUDIO_FREQUENCY_MASK       0x3FFF

#define SMARTAUDIO_CMD_GET_SETTINGS     0x03
#define SMARTAUDIO_CMD_SET_POWER        0x05
#define SMARTAUDIO_CMD_SET_CHANNEL      0x07
#define SMARTAUDIO_CMD_SET_FREQUENCY    0x09
#define SMARTAUDIO_CMD_SET_MODE         0x0B

#define SMARTAUDIO_RSP_GET_SETTINGS_V1  SMARTAUDIO_CMD_GET_SETTINGS >> 1
#define SMARTAUDIO_RSP_GET_SETTINGS_V2  (SMARTAUDIO_CMD_GET_SETTINGS >> 1) | 0x08
#define SMARTAUDIO_RSP_GET_SETTINGS_V21 (SMARTAUDIO_CMD_GET_SETTINGS >> 1) | 0x10
#define SMARTAUDIO_RSP_SET_POWER        SMARTAUDIO_CMD_SET_POWER >> 1
#define SMARTAUDIO_RSP_SET_CHANNEL      SMARTAUDIO_CMD_SET_CHANNEL >> 1
#define SMARTAUDIO_RSP_SET_FREQUENCY    SMARTAUDIO_CMD_SET_FREQUENCY >> 1
#define SMARTAUDIO_RSP_SET_MODE         SMARTAUDIO_CMD_SET_MODE >> 1

#define SMARTAUDIO_BANDCHAN_TO_INDEX(band, channel) (band * 8 + (channel))

#define SMARTAUDIO_SPEC_PROTOCOL_v1  0
#define SMARTAUDIO_SPEC_PROTOCOL_v2  1
#define SMARTAUDIO_SPEC_PROTOCOL_v21 2


    // POWER LEVELS: 3 protocols, 4 readings for output in mw
    const uint16_t POWER_LEVELS[3][4] =
    {
        //   25      200   500      800  mw
        //   14      23     27      29  dbm
        {    7 ,     16,    25,     40}, /* Version 1 */
        {    0 ,     1 ,    2 ,     3 }, /* Version 2 */
        {    14 ,    23 ,   27 ,    29 } /* Version 2.1 DBM MSB MUST BE SET TO 1 */
    };


    const uint16_t POW_MW_DBM_REL_TABLE[2][6]=
    {
        {14,  20 ,  23 , 26 , 27 , 29 },    /* DBM */
        {25,  100,  200, 400, 500, 800}     /* MW  */
    };

#define SA_DEBUG     // DEV TESTS ACTIVATE

class AP_SmartAudio
{
public:
        // Statistical counters, for user side trouble shooting.
        struct smartAudioStat_s {
            uint16_t pktsent;
            uint16_t pktrcvd;
            uint16_t badpre;
            uint16_t badlen;
            uint16_t crc;
            uint16_t ooopresp;
            uint16_t badcode;

            // request settings counter
            uint16_t rqsettings;
            uint16_t other_commands;

        } _saStat {};

        // Proposed to be into AP_VideoTX
        enum class HWVtxUpdates {
            OVERALL_UPD = 0X0F,
            FREQ_UPD = 1 << 0,
            CHAN_UPD = 1 << 1,
            POW_UPD  =  1 << 2,
            MODE_UPD =  1 << 3,
            NO_UPD   = 0
        };

     // FROM BETAFLIGHT

     struct smartaudioSettings_t {
        uint8_t  version;
        uint8_t  unlocked;
        uint8_t  channel;
        uint8_t  power;
        uint16_t frequency;
        uint8_t  band;

        uint8_t* power_levels=nullptr;
        uint8_t  power_in_dbm;

        uint16_t pitmodeFrequency;
        bool userFrequencyMode=false;     // user is setting freq
        bool pitModeRunning=false;
        bool pitmodeInRangeActive=false;
        bool pitmodeOutRangeActive=false;

        // smart audio autoadjust baudrate direction

        // current baud rate defined by autobaud

        //  |0 0 0 0 1 1 1 1|    // overall updated
        //  |0 0 0 0 0 0 0 1|    // freq updated    1 << 0
        //  |0 0 0 0 0 0 1 0|    // channel updated 1 << 1
        //  |0 0 0 0 0 1 0 0|    // power updated 1 << 2
        //  |0 0 0 0 1 0 0 0|    // mode updated 1 << 3
        uint8_t update_flags=0X00;

        bool initialized;

    } PACKED;

    struct smartaudioFrameHeader_t {
        //   uint16_t startCode;
        uint8_t syncByte;
        uint8_t headerByte;
        uint8_t command;
        uint8_t length;
    } PACKED;

    struct smartaudioCommandOnlyFrame_t {
        smartaudioFrameHeader_t header;
        uint8_t crc;
    } PACKED;

    struct smartaudioU8Frame_t {
        smartaudioFrameHeader_t header;
        uint8_t payload;
        uint8_t crc;
    } PACKED;

    struct smartaudioU16Frame_t {
        smartaudioFrameHeader_t header;
        uint16_t payload;
        uint8_t crc;
    } PACKED;

    struct smartaudioU8ResponseFrame_t {
        smartaudioFrameHeader_t header;
        uint8_t payload;
        uint8_t reserved;
        uint8_t crc;
    } PACKED;

    struct smartaudioU16ResponseFrame_t {
        smartaudioFrameHeader_t header;
        uint16_t payload;
        uint8_t reserved;
        uint8_t crc;
    } PACKED;

    struct smartaudioSettingsResponseFrame_t {
        smartaudioFrameHeader_t header;
        uint8_t channel;
        uint8_t power;
        uint8_t operationMode;
        uint16_t frequency;
        uint8_t crc;
    } PACKED;

    struct smartaudioSettingsExtendedResponseFrame_t {
        smartaudioFrameHeader_t header;
        uint8_t channel;
        uint8_t power;
        uint8_t operationMode;
        uint16_t frequency;
        uint8_t power_dbm;
        uint8_t power_levels_len;
        uint8_t* power_dbm_levels;
        uint8_t crc;
    } PACKED;

    // v 2.1 additions to response frame
    //0x0E (current power in dBm) 0x03 (amount of power levels) 0x00(dBm level 1) 0x0E (dBm level 2) 0x14 (dBm level 3) 0x1A (dBm level 4) 0x01(CRC8)
    union smartaudioFrame_t {
        smartaudioCommandOnlyFrame_t commandOnlyFrame;
        smartaudioU8Frame_t u8RequestFrame;
        smartaudioU16Frame_t u16RequestFrame;
    } PACKED;

    // request packet to be processed
    struct Packet{
        smartaudioFrame_t frame;
        uint8_t frame_size;
        uint32_t sended_at_ms;
    } PACKED;

    // value for current baud adjust
    int _sa_smartbaud=AP_SMARTAUDIO_UART_BAUD;
    int _sa_smartbaud_adjdir=1;


    // When enabled the settings returned from hw vtx are settled into ap_videoTx params NOT USED YET
    AP_Int8 _smart_audio_param_setup_defaults;
    AP_Int8 _smart_audio_param_hot_deploy;

    // hw vtx state control with 2 elements array use methods _push _peek
    uint8_t _vtx_state_idx=0;
    smartaudioSettings_t _vtx_states_buffer[2];
    smartaudioSettings_t *_vtx_current_state;

    // ready to go
    volatile bool initialised;

     // RingBuffer to store outgoing request.
    ObjectBuffer<Packet> requests_queue{SMARTAUDIO_BUFFER_CAPACITY};

    // time the last_request is process
    uint32_t _last_request_sended_at_ms;

    // loops is waiting a response after a request
    bool _is_waiting_response=false;

    // debug printint throttle control
    uint32_t _debug_last_printing_time_ms=0;

    AP_SmartAudio();

    static AP_SmartAudio *get_singleton(void)
    {
        return _singleton;
    }

    /* Do not allow copies */
    AP_SmartAudio(const AP_SmartAudio &other) = delete;

    AP_SmartAudio &operator=(const AP_SmartAudio&) = delete;

    static const struct AP_Param::GroupInfo var_info[];

    // init threads and lookup for io uart.
    bool init();

    // looping over request loop
    void loop();

    // updates the smartaudio state in sync whith AP_VideoTX
    bool update(bool force);

    // sends a frame over the wire
    void send_request(const smartaudioFrame_t& requestFrame, uint8_t size);

    // receives a frame response over the wire
    bool read_response(uint8_t *response_buffer);

    // parses the response and updates the AP_VTX readings
    bool parse_frame_response(const uint8_t *buffer);

    // get last reading from the fifo queue
    bool get_readings(AP_VideoTX *vtx_dest);

    // enqueue a get settings request
    void request_settings();

    void set_operation_mode(uint8_t mode);

    // enqueue a set frequency request, specifiying if the setted frequency is for pit mode or not
    void set_frequency(uint16_t frequency, bool isPitModeFreq);

    // enqueue a set channel request
    void set_channel(uint8_t chan);

    // enqueue a get pit mode frequency request
    void request_pit_mode_frequency();

    // enqueue a set power request
    void set_power(uint16_t power_mw, uint16_t dbm);

    void _push_vtx_state(smartaudioSettings_t state);

    void _peek_vtx_state(smartaudioSettings_t& state_holder){
        if(!initialised){
            return;
        }

        memcpy(&state_holder,_vtx_current_state,sizeof(smartaudioSettings_t));
        return;
    }




private:

    // FOR ARDUPILOT

    // serial interface
    AP_HAL::UARTDriver *_port;                  // UART used to send data to SmartAudio VTX

    //Pointer to singleton
    static AP_SmartAudio* _singleton;

    // response buffer length, permit splitted responses
    uint8_t _inline_buffer_length = 0;
    // expected packet size
    uint8_t _packet_size = 0;

    // utility method for debugging
    void _print_state(smartaudioSettings_t& state,bool details,bool updates,bool ap_video_tx_details);

    // utility method to print stats
    void _print_stats(bool errors,bool autobauding, bool commands_counters);

#ifdef SA_DEBUG
    // utility method for debugging.
    void _print_bytes_to_hex_string(const char* msg, const uint8_t buf[], uint8_t x,uint8_t offset);
#endif
    // utility method to get mw transformation from power in dbm
    static uint16_t _get_power_in_mw_from_dbm(uint8_t power){
        for (uint8_t i=0;i<7;i++){
            if (POW_MW_DBM_REL_TABLE[0][i]==uint16_t(power)){
                return POW_MW_DBM_REL_TABLE[1][i];
            }
        }

        return uint16_t(roundf(powf(10, power / 10.0f)));
    }


    // utility method to get mw transformation from power
    static uint16_t _get_power_in_dbm_from_mw(uint16_t power){
        for (uint8_t i=0;i<7;i++){
            if (POW_MW_DBM_REL_TABLE[1][i]==uint16_t(power)){
                return POW_MW_DBM_REL_TABLE[0][i];
            }
        }

        return uint16_t(roundf(powf(10, power / 10.0f)));
    }

    // returns the power_level applicable when request set power, version 2.1 return the MSB power bit masked at 1
    static uint8_t _get_power_level_from_dbm(uint8_t sma_version, uint8_t power){

        uint16_t powerLevel=0x00;

        // check valid version spec note sma_version is unsigned
        if (sma_version>SMARTAUDIO_SPEC_PROTOCOL_v21) {
            return 0;
        }

        if (sma_version==SMARTAUDIO_SPEC_PROTOCOL_v21){
            // set MSB BIT TO ONE AS SPEC SAYS
            return power|=128;
        }

        // SEARCH IN POWER_LEVELS TABLE
        for (uint8_t i=0;i<4 && sma_version<3;i++){
            if (POWER_LEVELS[2][i]==uint16_t(power)){
                powerLevel=POWER_LEVELS[sma_version][i];
            }
        }
       return powerLevel;
    }


    // utility method to get power in dbm mapping to power levels
    static uint8_t _get_power_in_dbm_from_vtx_power_level(uint8_t power_level, uint8_t& protocol_version)
    {
        for (uint8_t j = 0; j < 4; j++) {
            if (POWER_LEVELS[protocol_version][j] == power_level) {
                return POWER_LEVELS[2][j];
            }
        }
        return 0;
    }

    // returns the dbs associated by power-level input
    static uint8_t _get_power_in_dbm_from_vtx_power_level(uint8_t power_level, uint8_t& protocol_version, uint8_t& power_in_dbm)
    {
        power_in_dbm=_get_power_in_dbm_from_vtx_power_level(power_level,protocol_version);
        return power_in_dbm;
    }

    // enqueue a set power request using power_level, spec versions 1 and 2 or dbm value for spec version 2.1
    void set_power(uint8_t power_level);

    // sync the AP_VideoTx backend with the configured params.
    bool hot_deploy();

    // utility debug method
    void _print_debug_info();

    // return true if power need to bee synced with the backed
    bool sync_power();

    // TODO: Remove to use the AP self transformer
    static u_int16_t applyBigEndian16(u_int16_t bytes);


    // FROM BETAFLIGHT
    static void smartaudioFrameInit(const uint8_t command, smartaudioFrameHeader_t *header, const uint8_t payloadLength);
    static void smartaudioUnpackOperationMode(smartaudioSettings_t *settings, const uint8_t operationMode, const bool settingsResponse);
    static void smartaudioUnpackFrequency(smartaudioSettings_t *settings, const uint16_t frequency);
    static void smartaudioUnpackSettings(smartaudioSettings_t *settings, const smartaudioSettingsResponseFrame_t *frame);
    static void smartaudioUnpackSettings(smartaudioSettings_t *settings, const smartaudioSettingsExtendedResponseFrame_t *frame);
    static uint8_t smartaudioPackOperationMode(const smartaudioSettings_t *settings);

    size_t smartaudioFrameGetSettings(smartaudioFrame_t *smartaudioFrame);
    size_t smartaudioFrameGetPitmodeFrequency(smartaudioFrame_t *smartaudioFrame);
    size_t smartaudioFrameSetPower(smartaudioFrame_t *smartaudioFrame, const uint8_t power);
    size_t smartaudioFrameSetChannel(smartaudioFrame_t *smartaudioFrame, const uint8_t channel);
    size_t smartaudioFrameSetBandChannel(smartaudioFrame_t *smartaudioFrame, const uint8_t band, const uint8_t channel);
    size_t smartaudioFrameSetFrequency(smartaudioFrame_t *smartaudioFrame, const uint16_t frequency, const bool pitmodeFrequency);
    size_t smartaudioFrameSetOperationMode(smartaudioFrame_t *smartaudioFrame, const smartaudioSettings_t *settings);
    bool smartaudioParseResponseBuffer(smartaudioSettings_t *settings, const uint8_t *buffer);

    // change baud automatically when request-response fails many times
    void saAutobaud();
    void advice_gcs(uint8_t command);

};
#endif
