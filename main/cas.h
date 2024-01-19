#pragma once


/*
 * CAS format conversions
 */

#include <cstdint>
#include <vector>
#include <array>
#include <cstdio>
#include <cmath>

namespace cas 
{

typedef enum {
    CAS_UNKNOWN = -1,
    CAS_CSAVE = 0,  // CSAVE
    CAS_BSAVE = 1,  // BSAVE
    CAS_MSX = 2     // BASIC-Korvet
} caskind_t;

typedef std::vector<uint8_t> casdata_t;
typedef std::vector<uint8_t> wavdata_t;

constexpr int v06c_preamble_size = 256;
const std::array<uint8_t, 4>    V06C_BAS { 0xD3, 0xD3, 0xD3, 0xD3 }; // CSAVE
const std::array<uint8_t, 4>    V06C_BIN { 0xD2, 0xD2, 0xD2, 0xD2 }; // BSAVE/Monitor

/* MSX headers definitions */
const std::array<uint8_t, 8>    MSX_HEADER { 0x1F,0xA6,0xDE,0xBA,0xCC,0x13,0x7D,0x74 };
const std::array<uint8_t, 10>   MSX_ASCII  { 0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA };
const std::array<uint8_t, 10>   MSX_BIN    { 0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0 };
const std::array<uint8_t, 10>   MSX_BASIC  { 0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3 };

constexpr int long_pulse = std::ceil((float)CAS_LOAD_SAMPLERATE / 1200);
constexpr int short_pulse = std::ceil((float)CAS_LOAD_SAMPLERATE / 2400);

constexpr int short_silence = CAS_LOAD_SAMPLERATE;
constexpr int long_silence = CAS_LOAD_SAMPLERATE * 2;

constexpr int long_header = 16000;
constexpr int short_header = 4000;

// normal v06c encoding
wavdata_t biphase_encode(const casdata_t & data)
{
    wavdata_t w(data.size() * 8 * CAS_PSK_HALFPERIOD * 2);

    int wrindex = 0;
    for (int i = 0; i < data.size(); ++i) {
        uint8_t octet = data[i];
        for (int b = 0; b < 8; ++b, octet <<= 1) {
            int phase = (octet & 0x80) ? 32 : (255 - 32);
            for (int q = 0; q < CAS_PSK_HALFPERIOD; ++q) w[wrindex++] = phase;
            phase ^= 255;
            for (int q = 0; q < CAS_PSK_HALFPERIOD; ++q) w[wrindex++] = phase;
        }
    }

    return w;
}

void fsk_pulse(wavdata_t & w, int len)
{
    for (int i = 0; i < (len >> 1); ++i) w.push_back(255);
    for (int i = 0; i < (len >> 1); ++i) w.push_back(0);
}

void fsk_byte(wavdata_t & w, uint8_t octet)
{
    fsk_pulse(w, long_pulse); // start bit
    for (int b = 0; b < 8; ++b, octet >>= 1)
    {
        if (octet & 1) {
            fsk_pulse(w, short_pulse);
            fsk_pulse(w, short_pulse);
        }
        else {
            fsk_pulse(w, long_pulse);
        }
    }
    for (int i = 0; i < 4; ++i) {
        fsk_pulse(w, short_pulse); // 2x stop bit
    }
}

// reference
// https://github.com/oboroc/msx-books/blob/master/msx2-fb-1993-ru.md#10
// frequencies: 0 = 1200Hz / 1 = 2400Hz

// encode data until the next header is encountered, or data ends
int fsk_data(wavdata_t & w, const casdata_t & casfile, int pos)
{
    while (pos < casfile.size()) {
        if (pos % 8 == 0 && pos + 8 < casfile.size()) {
            // check header on every 8-byte boundary
            if (std::equal(MSX_HEADER.begin(), MSX_HEADER.end(), &casfile[pos])) {
                printf("%s found header at pos=%d\n", __FUNCTION__, pos);
                return pos;
            }
        }
        
        uint8_t octet = casfile[pos];
        //printf("%s pos=%04x octet=%02x\n", __FUNCTION__, pos, octet);
        fsk_byte(w, octet);
        ++pos;
    }

    return pos;
}

void fsk_silence(wavdata_t & w, int len)
{
    for (int i = 0; i < len; ++i) {
        w.push_back(128);
    }
}

void fsk_header(wavdata_t & w, int len)
{
    for (int i = 0; i < len; ++i) {
        fsk_pulse(w, short_pulse);
    }
}

wavdata_t msx_encode(const casdata_t & casfile)
{
    wavdata_t w;
    const size_t data_samps = casfile.size() * long_pulse * (1 + 8 + 2);            // data samples, each byte has 1 start bit, 8 data bits, 2 stop bits
    const size_t headers_samps = (long_header + short_header) * short_pulse * 2;    // at least two headers, most likely only two
    const size_t silence_samps = short_silence;                                     // expecting one short silence
    w.reserve(data_samps + headers_samps + silence_samps);

    #if 0
    printf("%s reserved %d\n", __FUNCTION__, data_samps + headers_samps + silence_samps);
    #endif

    int block_num = 0;
    int pos = 0;
    while (pos + MSX_HEADER.size() < casfile.size()) {
        if (std::equal(MSX_HEADER.begin(), MSX_HEADER.end(), &casfile[pos])) {
            fsk_header(w, block_num == 0 ? long_header : short_header);
            pos += MSX_HEADER.size();

            pos = fsk_data(w, casfile, pos);
            fsk_silence(w, short_silence);
            ++block_num;
        }
        else {
            // invalid cas? scan for a header
            ++pos;
        }
    }

    #if 0
    printf("%s nsamps=%d\n", __FUNCTION__, w.size());
    #endif

    return w;
}

wavdata_t v06c_encode(const casdata_t & casfile)
{
    wavdata_t raw;
    raw.resize(v06c_preamble_size + casfile.size());
    for (int i = 0; i < v06c_preamble_size - 1; ++i) raw[i] = 0;
    raw[v06c_preamble_size - 1] = 0xe6;
    for (int i = 0; i < casfile.size(); ++i) raw[i + v06c_preamble_size] = casfile[i];
    #if 0
    for (int i = 0; i < raw.size(); ++i) {
        printf("%02x ", raw[i]);
        if ((i + 1) % 16 == 0) {
            putchar('\n');
        }
    }
    #endif

    return biphase_encode(raw);
}

caskind_t cas_guess_kind(const casdata_t & casfile)
{
    if (casfile.size() < V06C_BAS.size()) return CAS_UNKNOWN;
    if (std::equal(V06C_BAS.begin(), V06C_BAS.end(), casfile.begin())) return CAS_CSAVE;
    if (std::equal(V06C_BIN.begin(), V06C_BIN.end(), casfile.begin())) return CAS_BSAVE;

    if (casfile.size() < MSX_HEADER.size()) return CAS_UNKNOWN;
    if (std::equal(MSX_HEADER.begin(), MSX_HEADER.end(), casfile.begin())) return CAS_MSX;

    return CAS_UNKNOWN;
}

// v06c basic cas file:
//      D3 D3 D3 D3 ...  data  -- append preamble
// basic korvet cas file:
//      https://www.msx.org/wiki/Emulation_related_file_formats#.CAS
wavdata_t encode_cas(const wavdata_t & casfile)
{
    switch (cas_guess_kind(casfile)) {
        case CAS_BSAVE:
        case CAS_CSAVE:
            return v06c_encode(casfile);
        case CAS_MSX:
            return msx_encode(casfile);
        default:
            break;
    }

    return {};
}

}