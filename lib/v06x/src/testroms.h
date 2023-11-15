#pragma once

extern "C" {
    extern unsigned char wave_rom[];
    extern unsigned int wave_rom_len;
    extern unsigned char oblitterated_rom[];
    extern unsigned int oblitterated_rom_len;
    extern unsigned char arzak_rom[];
    extern unsigned int arzak_rom_len;
    extern unsigned char v06x_rom[];
    extern unsigned int v06x_rom_len;
    extern unsigned char eightsnail_rom[];
    extern unsigned int eightsnail_rom_len;
    extern unsigned char s8snail_rom[];
    extern unsigned int s8snail_rom_len;
    extern unsigned char clrs_rom[];
    extern unsigned int clrs_rom_len;
    extern unsigned char clrspace_rom[];
    extern unsigned int clrspace_rom_len;
    extern unsigned char kittham1_rom[];
    extern unsigned int kittham1_rom_len;
    extern unsigned char tiedye2_rom[];
    extern unsigned int tiedye2_rom_len;
    extern unsigned char bord_rom[];
    extern unsigned int bord_rom_len;
    extern unsigned char bord2_rom[];
    extern unsigned int bord2_rom_len;
    extern unsigned char bazis_rom[];
    extern unsigned int bazis_rom_len;
    extern unsigned char sunsetb_rom[];
    extern unsigned int sunsetb_rom_len;
    extern unsigned char hscroll_rom[];
    extern unsigned int hscroll_rom_len;
    extern unsigned char mclrs_rom[];
    extern unsigned int mclrs_rom_len;
    extern unsigned char progdemo_rom[];
    extern unsigned int progdemo_rom_len;
    extern unsigned char bolderm_rom[];
    extern unsigned int bolderm_rom_len;
    extern unsigned char cronex_rom[];
    extern unsigned int cronex_rom_len;
    extern unsigned char cyberunp_rom[];
    extern unsigned int cyberunp_rom_len;
    extern unsigned char cybermut_rom[];
    extern unsigned int cybermut_rom_len;
    extern unsigned char spsmerti_rom[];
    extern unsigned int spsmerti_rom_len;
    extern unsigned char ses_rom[];
    extern unsigned int ses_rom_len;
    extern unsigned char bas299_rom[];
    extern unsigned int bas299_rom_len;
    extern unsigned char GameNoname_rom[];
    extern unsigned int GameNoname_rom_len;
    extern unsigned char testtp_rom[];
    extern unsigned int testtp_rom_len;
    extern unsigned char kdtest_rom[];
    extern unsigned int kdtest_rom_len;
    extern unsigned char kdadrtst_rom[];
    extern unsigned int kdadrtst_rom_len;
    extern unsigned char text80_rom[];
    extern unsigned int text80_rom_len;
    extern unsigned char dizrek__rom[];
    extern unsigned int dizrek__rom_len;

    extern unsigned char incurzion_rom[];
    extern unsigned int incurzion_rom_len;
    extern unsigned char baskor_rom[];
    extern unsigned int baskor_rom_len;

}

#define ROM(X) X##_rom
#define ROMLEN(X) X##_rom_len
