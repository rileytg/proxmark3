//-----------------------------------------------------------------------------
// Copyright (C) 2017 October, Satsuoni
// 2017 iceman
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// High frequency ISO18092 / FeliCa commands
//-----------------------------------------------------------------------------
#include "cmdhffelica.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "cmdparser.h"    // command_t
#include "comms.h"
#include "cmdtrace.h"
#include "crc16.h"

#include "ui.h"
#include "mifare.h"     // felica_card_select_t struct

static int CmdHelp(const char *Cmd);

/*
static int usage_hf_felica_sim(void) {
    PrintAndLogEx(NORMAL, "\n Emulating ISO/18092 FeliCa tag \n");
    PrintAndLogEx(NORMAL, "Usage: hf felica sim [h] t <type> [v]");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h     : This help");
    PrintAndLogEx(NORMAL, "    t     : 1 = FeliCa");
    PrintAndLogEx(NORMAL, "          : 2 = FeliCaLiteS");
    PrintAndLogEx(NORMAL, "    v     : (Optional) Verbose");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, "          hf felica sim t 1 ");
    return PM3_SUCCESS;
}
*/

static int usage_hf_felica_sniff(void) {
    PrintAndLogEx(NORMAL, "It get data from the field and saves it into command buffer.");
    PrintAndLogEx(NORMAL, "Buffer accessible from command 'hf list felica'");
    PrintAndLogEx(NORMAL, "Usage:  hf felica sniff <s> <t>");
    PrintAndLogEx(NORMAL, "      s       samples to skip (decimal)");
    PrintAndLogEx(NORMAL, "      t       triggers to skip (decimal)");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, "          hf felica sniff s 1000");
    return PM3_SUCCESS;
}
static int usage_hf_felica_simlite(void) {
    PrintAndLogEx(NORMAL, "\n Emulating ISO/18092 FeliCa Lite tag \n");
    PrintAndLogEx(NORMAL, "Usage: hf felica litesim [h] u <uid>");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h     : This help");
    PrintAndLogEx(NORMAL, "    uid   : UID in hexsymbol");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, "          hf felica litesim 11223344556677");
    return PM3_SUCCESS;
}
static int usage_hf_felica_dumplite(void) {
    PrintAndLogEx(NORMAL, "\n Dump ISO/18092 FeliCa Lite tag \n");
    PrintAndLogEx(NORMAL, "press button to abort run, otherwise it will loop for 200sec.");
    PrintAndLogEx(NORMAL, "Usage: hf felica litedump [h]");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h     : This help");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, "          hf felica litedump");
    return PM3_SUCCESS;
}
static int usage_hf_felica_raw(void) {
    PrintAndLogEx(NORMAL, "Usage: hf felica raw [-h] [-r] [-c] [-p] [-a] <0A 0B 0C ... hex>");
    PrintAndLogEx(NORMAL, "       -h    this help");
    PrintAndLogEx(NORMAL, "       -r    do not read response");
    PrintAndLogEx(NORMAL, "       -c    calculate and append CRC");
    PrintAndLogEx(NORMAL, "       -p    leave the signal field ON after receive");
    PrintAndLogEx(NORMAL, "       -a    active signal field ON without select");
    PrintAndLogEx(NORMAL, "       -s    active signal field ON with select");
    return PM3_SUCCESS;
}

static int usage_hf_felica_request_service(void) {
    PrintAndLogEx(NORMAL, "\nInfo: Use this command to verify the existence of Area and Service, and to acquire Key Version:");
    PrintAndLogEx(NORMAL, "       - When the specified Area or Service exists, the card returns Key Version.");
    PrintAndLogEx(NORMAL, "       - When the specified Area or Service does not exist, the card returns FFFFh as Key Version.");
    PrintAndLogEx(NORMAL, "\nUsage: hf felica rqservice [-h] <0A 0B 0C ... IDm hex> <01 Number of Node hex> <0A 0B Node Code List hex (Little Endian)> <0A 0B CRC hex>");
    PrintAndLogEx(NORMAL, "       -h    this help");
    PrintAndLogEx(NORMAL, "       -c    calculate and append CRC");
    PrintAndLogEx(NORMAL, "\nExample: hf felica rqservice 01100910c11bc407 01 FFFF 2837\n\n");
    return PM3_SUCCESS;
}

/*
 * Parses line spacing and tabs.
 * Returns 1 if the given char is a space or tab
 */
static int parse_cmd_parameter_separator(const char *Cmd, int i){
    return Cmd[i] == ' ' || Cmd[i] == '\t' ? 1 : 0;
}

/*
 * Counts and sets the number of commands.
 */
static void strip_cmds(const char *Cmd){
    PrintAndLogEx(NORMAL, "CMD count: %i", strlen(Cmd));
    while (*Cmd == ' ' || *Cmd == '\t'){
        PrintAndLogEx(NORMAL, "CMD: %s", Cmd);
        Cmd++;
    }
    PrintAndLogEx(NORMAL, "CMD string: %s", Cmd);
}

/**
 * Checks if a char is a hex value.
 * @param Cmd
 * @return one if it is a valid hex char. Zero if not a valid hex char.
 */
static bool is_hex_input(const char *Cmd, int i){
    return (Cmd[i] >= '0' && Cmd[i] <= '9') || (Cmd[i] >= 'a' && Cmd[i] <= 'f') || (Cmd[i] >= 'A' && Cmd[i] <= 'F') ? 1 : 0;
}

/**
 *
 * @param Extracts the data from the cmd and puts it into the data array.
 */
static void get_cmd_data(const char *Cmd, int i, uint16_t datalen, uint8_t *data, char buf[]){
    uint32_t temp;
    if (strlen(buf) >= 2) {
        sscanf(buf, "%x", &temp);
        data[datalen] = (uint8_t)(temp & 0xff);
        *buf = 0;
    }
}

/*
static int usage_hf_felica_dump(void) {
    // TODO IMPLEMENT
    PrintAndLogEx(NORMAL, "Usage: hf felica dump [-h] <outputfile>");
    PrintAndLogEx(NORMAL, "       -h    this help");
    return PM3_SUCCESS;
}*/

static int CmdHFFelicaList(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    //PrintAndLogEx(NORMAL, "Deprecated command, use 'hf list felica' instead");
    CmdTraceList("felica");
    return PM3_SUCCESS;
}

static int CmdHFFelicaReader(const char *Cmd) {
    bool verbose = !(tolower(Cmd[0]) == 's');
    return readFelicaUid(verbose);
}

/*
static int CmdHFFelicaDump(const char *Cmd) {
    if (strlen(Cmd) < 1) return usage_hf_felica_dump();
    // TODO IMPLEMENT
    return PM3_SUCCESS;
}*/

/**
 * Sends a request service frame
 * @return
 */
static int request_service() {
    return PM3_SUCCESS;
}

/**
 * Command parser for rqservice.
 * @param Cmd input data of the user.
 * @return client result code.
 */
static int CmdHFFelicaRequestService(const char *Cmd) {
    if (strlen(Cmd) < 2) return usage_hf_felica_request_service();
    int i = 0;
    uint8_t data[PM3_CMD_DATA_SIZE];
    bool crc = false;
    bool length = false;
    uint16_t datalen = 0;
    char buf[5] = "";

    strip_cmds(Cmd);
    while (Cmd[i] != '\0') {
        PrintAndLogEx(NORMAL, "Parse String %s: ", Cmd);
        PrintAndLogEx(NORMAL, "i = %i: ", i);
        if (Cmd[i] == '-') {
            switch (Cmd[i + 1]) {
                case 'H':
                case 'h':
                    return usage_hf_felica_raw();
                case 'c':
                    crc = true;
                    break;
                case 'l':
                    length = true;
                    break;
                default:
                    return usage_hf_felica_raw();
            }
            i += 2;
        }
        PrintAndLogEx(NORMAL, "i after single params = %i: ", i);
        i = i + parse_cmd_parameter_separator(Cmd, i);
        PrintAndLogEx(NORMAL, "i after cnd separator: %i", i);
        if (is_hex_input(Cmd, i)){
            buf[strlen(buf) + 1] = 0;
            buf[strlen(buf)] = Cmd[i];
            i++;
            PrintAndLogEx(NORMAL, "i after is hex input: %i", i);
            get_cmd_data(Cmd, i, datalen, data, buf);

        }else {
          i++;
        }
    }
    request_service();
    clearCommandBuffer();
    return PM3_SUCCESS;
}

static int CmdHFFelicaNotImplementedYet(const char *Cmd) {
    PrintAndLogEx(NORMAL, "Feature not implemented Yet!");
    return PM3_SUCCESS;
}

// simulate iso18092 / FeliCa tag
// Commented, there is no counterpart in ARM at the moment
/*
static int CmdHFFelicaSim(const char *Cmd) {
    bool errors = false;
    uint8_t flags = 0;
    uint8_t tagtype = 1;
    uint8_t cmdp = 0;
    uint8_t uid[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int uidlen = 0;
    bool verbose =  false;

    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (param_getchar(Cmd, cmdp)) {
            case 'h':
            case 'H':
                return usage_hf_felica_sim();
            case 't':
            case 'T':
                // Retrieve the tag type
                tagtype = param_get8ex(Cmd, cmdp + 1, 0, 10);
                if (tagtype == 0)
                    errors = true;
                cmdp += 2;
                break;
            case 'u':
            case 'U':
                // Retrieve the full 4,7,10 byte long uid
                param_gethex_ex(Cmd, cmdp + 1, uid, &uidlen);
                if (!errors) {
                    PrintAndLogEx(NORMAL, "Emulating ISO18092/FeliCa tag with %d byte UID (%s)", uidlen >> 1, sprint_hex(uid, uidlen >> 1));
                }
                cmdp += 2;
                break;
            case 'v':
            case 'V':
                verbose = true;
                cmdp++;
                break;
            case 'e':
            case 'E':
                cmdp++;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter '%c'", param_getchar(Cmd, cmdp));
                errors = true;
                break;
        }
    }

    //Validations
    if (errors || cmdp == 0) return usage_hf_felica_sim();

    clearCommandBuffer();
    SendCommandOLD(CMD_HF_FELICA_SIMULATE,  tagtype, flags, 0, uid, uidlen >> 1);
    PacketResponseNG resp;

    if (verbose)
        PrintAndLogEx(NORMAL, "Press pm3-button to abort simulation");

    while (!kbd_enter_pressed()) {
        if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) continue;
    }
    return PM3_SUCCESS;
}
*/

static int CmdHFFelicaSniff(const char *Cmd) {
    uint8_t cmdp = 0;
    uint64_t samples2skip = 0;
    uint64_t triggers2skip = 0;
    bool errors = false;

    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (param_getchar(Cmd, cmdp)) {
            case 'h':
            case 'H':
                return usage_hf_felica_sniff();
            case 's':
            case 'S':
                samples2skip = param_get32ex(Cmd, cmdp + 1, 0, 10);
                cmdp += 2;
                break;
            case 't':
            case 'T':
                triggers2skip = param_get32ex(Cmd, cmdp + 1, 0, 10);
                cmdp += 2;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter '%c'", param_getchar(Cmd, cmdp));
                errors = true;
                break;
        }
    }
    //Validations
    if (errors || cmdp == 0) return usage_hf_felica_sniff();

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_FELICA_SNIFF, samples2skip, triggers2skip, 0, NULL, 0);
    return PM3_SUCCESS;
}

// uid  hex
static int CmdHFFelicaSimLite(const char *Cmd) {
    uint64_t uid = param_get64ex(Cmd, 0, 0, 16);

    if (!uid)
        return usage_hf_felica_simlite();

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_FELICALITE_SIMULATE, uid, 0, 0, NULL, 0);
    return PM3_SUCCESS;
}

static void printSep() {
    PrintAndLogEx(NORMAL, "------------------------------------------------------------------------------------");
}

static uint16_t PrintFliteBlock(uint16_t tracepos, uint8_t *trace, uint16_t tracelen) {
    if (tracepos + 19 >= tracelen)
        return tracelen;

    trace += tracepos;
    uint8_t blocknum = trace[0];
    uint8_t status1 = trace[1];
    uint8_t status2 = trace[2];

    char line[110] = {0};
    for (int j = 0; j < 16; j++) {
        snprintf(line + (j * 4), sizeof(line) - 1 - (j * 4), "%02x  ", trace[j + 3]);
    }

    PrintAndLogEx(NORMAL, "block number %02x, status: %02x %02x", blocknum, status1, status2);
    switch (blocknum) {
        case 0x00:
            PrintAndLogEx(NORMAL,  "S_PAD0: %s", line);
            break;
        case 0x01:
            PrintAndLogEx(NORMAL,  "S_PAD1: %s", line);
            break;
        case 0x02:
            PrintAndLogEx(NORMAL,  "S_PAD2: %s", line);
            break;
        case 0x03:
            PrintAndLogEx(NORMAL,  "S_PAD3: %s", line);
            break;
        case 0x04:
            PrintAndLogEx(NORMAL,  "S_PAD4: %s", line);
            break;
        case 0x05:
            PrintAndLogEx(NORMAL,  "S_PAD5: %s", line);
            break;
        case 0x06:
            PrintAndLogEx(NORMAL,  "S_PAD6: %s", line);
            break;
        case 0x07:
            PrintAndLogEx(NORMAL,  "S_PAD7: %s", line);
            break;
        case 0x08:
            PrintAndLogEx(NORMAL,  "S_PAD8: %s", line);
            break;
        case 0x09:
            PrintAndLogEx(NORMAL,  "S_PAD9: %s", line);
            break;
        case 0x0a:
            PrintAndLogEx(NORMAL,  "S_PAD10: %s", line);
            break;
        case 0x0b:
            PrintAndLogEx(NORMAL,  "S_PAD11: %s", line);
            break;
        case 0x0c:
            PrintAndLogEx(NORMAL,  "S_PAD12: %s", line);
            break;
        case 0x0d:
            PrintAndLogEx(NORMAL,  "S_PAD13: %s", line);
            break;
        case 0x0E: {
            uint32_t regA = trace[3] | trace[4] << 8 | trace[5] << 16 | trace[ 6] << 24;
            uint32_t regB = trace[7] | trace[8] << 8 | trace[9] << 16 | trace[10] << 24;
            line[0] = 0;
            for (int j = 0; j < 8; j++)
                snprintf(line + (j * 2), sizeof(line) - 1 - (j * 2), "%02x", trace[j + 11]);

            PrintAndLogEx(NORMAL,  "REG: regA: %d regB: %d regC: %s ", regA, regB, line);
        }
        break;
        case 0x80:
            PrintAndLogEx(NORMAL,  "Random Challenge, WO:  %s ", line);
            break;
        case 0x81:
            PrintAndLogEx(NORMAL,  "MAC, only set on dual read:  %s ", line);
            break;
        case 0x82: {
            char idd[20];
            char idm[20];
            for (int j = 0; j < 8; j++)
                snprintf(idd + (j * 2), sizeof(idd) - 1 - (j * 2), "%02x", trace[j + 3]);

            for (int j = 0; j < 6; j++)
                snprintf(idm + (j * 2), sizeof(idm) - 1 - (j * 2), "%02x", trace[j + 13]);

            PrintAndLogEx(NORMAL,  "ID Block, IDd: 0x%s DFC: 0x%02x%02x Arb: %s ", idd, trace[11], trace [12], idm);
        }
        break;
        case 0x83: {
            char idm[20];
            char pmm[20];
            for (int j = 0; j < 8; j++)
                snprintf(idm + (j * 2), sizeof(idm) - 1 - (j * 2), "%02x", trace[j + 3]);

            for (int j = 0; j < 8; j++)
                snprintf(pmm + (j * 2), sizeof(pmm) - 1 - (j * 2), "%02x", trace[j + 11]);

            PrintAndLogEx(NORMAL,  "DeviceId:  IDm: 0x%s PMm: 0x%s ", idm, pmm);
        }
        break;
        case 0x84:
            PrintAndLogEx(NORMAL,  "SER_C: 0x%02x%02x ", trace[3], trace[4]);
            break;
        case 0x85:
            PrintAndLogEx(NORMAL,  "SYS_Cl 0x%02x%02x ", trace[3], trace[4]);
            break;
        case 0x86:
            PrintAndLogEx(NORMAL,  "CKV (key version): 0x%02x%02x ", trace[3], trace[4]);
            break;
        case 0x87:
            PrintAndLogEx(NORMAL,  "CK (card key), WO:   %s ", line);
            break;
        case 0x88: {
            PrintAndLogEx(NORMAL,  "Memory Configuration (MC):");
            PrintAndLogEx(NORMAL,  "MAC needed to write state: %s", trace[3 + 12] ? "on" : "off");
            //order might be off here...
            PrintAndLogEx(NORMAL,  "Write with MAC for S_PAD  : %s ", sprint_bin(trace + 3 + 10, 2));
            PrintAndLogEx(NORMAL,  "Write with AUTH for S_PAD : %s ", sprint_bin(trace + 3 + 8, 2));
            PrintAndLogEx(NORMAL,  "Read after AUTH for S_PAD : %s ", sprint_bin(trace + 3 + 6, 2));
            PrintAndLogEx(NORMAL,  "MAC needed to write CK and CKV: %s", trace[3 + 5] ? "on" : "off");
            PrintAndLogEx(NORMAL,  "RF parameter: %02x", (trace[3 + 4] & 0x7));
            PrintAndLogEx(NORMAL,  "Compatible with NDEF: %s", trace[3 + 3] ? "yes" : "no");
            PrintAndLogEx(NORMAL,  "Memory config writable : %s", (trace[3 + 2] == 0xff) ? "yes" : "no");
            PrintAndLogEx(NORMAL,  "RW access for S_PAD : %s ", sprint_bin(trace + 3, 2));
        }
        break;
        case 0x90: {
            PrintAndLogEx(NORMAL,  "Write count, RO:   %02x %02x %02x ", trace[3], trace[4], trace[5]);
        }
        break;
        case 0x91: {
            PrintAndLogEx(NORMAL,  "MAC_A, RW (auth):   %s ", line);
        }
        break;
        case 0x92:
            PrintAndLogEx(NORMAL,  "State:");
            PrintAndLogEx(NORMAL,  "Polling disabled: %s", trace[3 + 8] ? "yes" : "no");
            PrintAndLogEx(NORMAL,  "Authenticated: %s", trace[3] ? "yes" : "no");
            break;
        case 0xa0:
            PrintAndLogEx(NORMAL,  "CRC of all blocks match : %s", (trace[3 + 2] == 0xff) ? "no" : "yes");
            break;
        default:
            PrintAndLogEx(WARNING,  "INVALID %d: %s", blocknum, line);
            break;
    }
    return tracepos + 19;
}

static int CmdHFFelicaDumpLite(const char *Cmd) {

    char ctmp = tolower(param_getchar(Cmd, 0));
    if (ctmp == 'h') return usage_hf_felica_dumplite();

    PrintAndLogEx(SUCCESS, "FeliCa lite - dump started");
    PrintAndLogEx(SUCCESS, "press pm3-button to cancel");
    clearCommandBuffer();
    SendCommandNG(CMD_HF_FELICALITE_DUMP, NULL, 0);
    PacketResponseNG resp;

    uint8_t timeout = 0;
    while (!WaitForResponseTimeout(CMD_ACK, &resp, 2000)) {
        timeout++;
        printf(".");
        fflush(stdout);
        if (kbd_enter_pressed()) {
            PrintAndLogEx(WARNING, "\n[!] aborted via keyboard!\n");
            DropField();
            return PM3_EOPABORTED;
        }
        if (timeout > 100) {
            PrintAndLogEx(WARNING, "timeout while waiting for reply.");
            DropField();
            return PM3_ETIMEOUT;
        }
    }
    if (resp.oldarg[0] == 0) {
        PrintAndLogEx(WARNING, "\nButton pressed. Aborted.");
        return PM3_EOPABORTED;
    }

    uint32_t tracelen = resp.oldarg[1];
    if (tracelen == 0) {
        PrintAndLogEx(WARNING, "\nNo trace data! Maybe not a FeliCa Lite card?");
        return PM3_ESOFT;
    }

    uint8_t *trace = calloc(tracelen, sizeof(uint8_t));
    if (trace == NULL) {
        PrintAndLogEx(WARNING, "Cannot allocate memory for trace");
        return PM3_EMALLOC;
    }

    if (!GetFromDevice(BIG_BUF, trace, tracelen, 0, NULL, 0, NULL, 2500, false)) {
        PrintAndLogEx(WARNING, "command execution time out");
        free(trace);
        return PM3_ETIMEOUT;
    }

    PrintAndLogEx(SUCCESS, "Recorded Activity (trace len = %"PRIu64" bytes)", tracelen);

    print_hex_break(trace, tracelen, 32);
    printSep();

    uint16_t tracepos = 0;
    while (tracepos < tracelen)
        tracepos = PrintFliteBlock(tracepos, trace, tracelen);

    printSep();

    free(trace);
    return PM3_SUCCESS;
}

static void waitCmdFelica(uint8_t iSelect) {
    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 2000)) {
        uint16_t len = iSelect ? (resp.oldarg[1] & 0xffff) : (resp.oldarg[0] & 0xffff);
        PrintAndLogEx(NORMAL, "Client Received %i octets", len);
        if (!len)
            return;
        PrintAndLogEx(NORMAL, "%s", sprint_hex(resp.data.asBytes, len));
        if(!check_crc(CRC_FELICA, resp.data.asBytes + 2, len - 2)){
            PrintAndLogEx(ERR, "Error: CRC of received bytes are incorrect!");
        }
    } else {
        PrintAndLogEx(WARNING, "Timeout while waiting for reply.");
    }
}

static int CmdHFFelicaCmdRaw(const char *Cmd) {
    bool reply = 1;
    bool crc = false;
    bool power = false;
    bool active = false;
    bool active_select = false;
    uint16_t numbits = 0;
    char buf[5] = "";
    int i = 0;
    uint8_t data[PM3_CMD_DATA_SIZE];
    uint16_t datalen = 0;
    uint32_t temp;

    if (strlen(Cmd) < 2) return usage_hf_felica_raw();

    // strip
    while (*Cmd == ' ' || *Cmd == '\t') Cmd++;

    while (Cmd[i] != '\0') {
        if (Cmd[i] == ' ' || Cmd[i] == '\t') { i++; continue; }
        if (Cmd[i] == '-') {
            switch (Cmd[i + 1]) {
                case 'H':
                case 'h':
                    return usage_hf_felica_raw();
                case 'r':
                    reply = false;
                    break;
                case 'c':
                    crc = true;
                    break;
                case 'p':
                    power = true;
                    break;
                case 'a':
                    active = true;
                    break;
                case 's':
                    active_select = true;
                    break;
                case 'b':
                    sscanf(Cmd + i + 2, "%d", &temp);
                    numbits = temp & 0xFFFF;
                    i += 3;
                    while (Cmd[i] != ' ' && Cmd[i] != '\0') { i++; }
                    i -= 2;
                    break;
                default:
                    return usage_hf_felica_raw();
            }
            i += 2;
            continue;
        }
        if ((Cmd[i] >= '0' && Cmd[i] <= '9') ||
                (Cmd[i] >= 'a' && Cmd[i] <= 'f') ||
                (Cmd[i] >= 'A' && Cmd[i] <= 'F')) {
            buf[strlen(buf) + 1] = 0;
            buf[strlen(buf)] = Cmd[i];
            i++;

            if (strlen(buf) >= 2) {
                sscanf(buf, "%x", &temp);
                data[datalen] = (uint8_t)(temp & 0xff);
                *buf = 0;
                if (++datalen >= sizeof(data)) {
                    if (crc)
                        PrintAndLogEx(NORMAL, "Buffer is full, we can't add CRC to your data");
                    break;
                }
            }
            continue;
        }
        PrintAndLogEx(WARNING, "Invalid char on input");
        return PM3_EINVARG;
    }

    if (crc && datalen > 0 && datalen < sizeof(data) - 2) {
        uint8_t b1, b2;
        compute_crc(CRC_FELICA, data, datalen, &b1, &b2);
        data[datalen++] = b2;
        data[datalen++] = b1;
    }

    uint8_t flags = 0;
    if (active || active_select) {
        flags |= FELICA_CONNECT;
        if (active)
            flags |= FELICA_NO_SELECT;
    }

    if (power) {
        flags |= FELICA_NO_DISCONNECT;
    }

    if (datalen > 0) {
        flags |= FELICA_RAW;
    }

    // Max buffer is PM3_CMD_DATA_SIZE
    datalen = (datalen > PM3_CMD_DATA_SIZE) ? PM3_CMD_DATA_SIZE : datalen;

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_FELICA_COMMAND, flags, (datalen & 0xFFFF) | (uint32_t)(numbits << 16), 0, data, datalen);

    if (reply) {
        if (active_select) {
            PrintAndLogEx(NORMAL, "Active select wait for FeliCa.");
            waitCmdFelica(1);
        }
        if (datalen > 0) {
            waitCmdFelica(0);
        }
    }
    return PM3_SUCCESS;
}

int readFelicaUid(bool verbose) {

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_FELICA_COMMAND, FELICA_CONNECT, 0, 0, NULL, 0);
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 2500)) {
        if (verbose) PrintAndLogEx(WARNING, "FeliCa card select failed");
        //SendCommandMIX(CMD_HF_FELICA_COMMAND, 0, 0, 0, NULL, 0);
        return PM3_ESOFT;
    }

    felica_card_select_t card;
    memcpy(&card, (felica_card_select_t *)resp.data.asBytes, sizeof(felica_card_select_t));
    uint64_t status = resp.oldarg[0];

    switch (status) {
        case 1: {
            if (verbose)
                PrintAndLogEx(WARNING, "card timeout");
            return PM3_ETIMEOUT;
        }
        case 2: {
            if (verbose)
                PrintAndLogEx(WARNING, "card answered wrong");
            return PM3_ESOFT;
        }
        case 3: {
            if (verbose)
                PrintAndLogEx(WARNING, "CRC check failed");
            return PM3_ESOFT;
        }
        case 0: {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(SUCCESS, "FeliCa tag info");

            PrintAndLogEx(NORMAL, "IDm  %s", sprint_hex(card.IDm, sizeof(card.IDm)));
            PrintAndLogEx(NORMAL, "  - CODE    %s", sprint_hex(card.code, sizeof(card.code)));
            PrintAndLogEx(NORMAL, "  - NFCID2  %s", sprint_hex(card.uid, sizeof(card.uid)));

            PrintAndLogEx(NORMAL, "Parameter (PAD) | %s", sprint_hex(card.PMm, sizeof(card.PMm)));
            PrintAndLogEx(NORMAL, "  - IC CODE %s", sprint_hex(card.iccode, sizeof(card.iccode)));
            PrintAndLogEx(NORMAL, "  - MRT     %s", sprint_hex(card.mrt, sizeof(card.mrt)));

            PrintAndLogEx(NORMAL, "SERVICE CODE %s", sprint_hex(card.servicecode, sizeof(card.servicecode)));
            break;
        }
    }
    return PM3_SUCCESS;
}

static command_t CommandTable[] = {
    {"----------- General -----------", CmdHelp,                IfPm3Iso14443a,  ""},
    {"help",      CmdHelp,              AlwaysAvailable, "This help"},
    {"list",      CmdHFFelicaList,      AlwaysAvailable,     "List ISO 18092/FeliCa history"},
    {"reader",    CmdHFFelicaReader,    IfPm3Felica,     "Act like an ISO18092/FeliCa reader"},
    {"sniff",     CmdHFFelicaSniff,     IfPm3Felica,     "Sniff ISO 18092/FeliCa traffic"},
    {"raw",       CmdHFFelicaCmdRaw,    IfPm3Felica,     "Send raw hex data to tag"},
    {"----------- FeliCa Standard (support in progress) -----------", CmdHelp,                IfPm3Iso14443a,  ""},
    //{"dump",    CmdHFFelicaDump,    IfPm3Felica,     "Wait for and try dumping FeliCa"},
    {"rqservice",    CmdHFFelicaRequestService,    IfPm3Felica,     "verify the existence of Area and Service, and to acquire Key Version."},
    {"rqresponse",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "verify the existence of a card and its Mode."},
    //{"rdNoEncryption",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "read Block Data from authentication-not-required Service."},
    //{"wrNoEncryption",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "write Block Data to an authentication-required Service."},
    //{"searchSvCode",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "acquire Area Code and Service Code."},
    //{"rqSysCode",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "acquire System Code registered to the card."},
    //{"auth1",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "authenticate a card."},
    //{"auth2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "allow a card to authenticate a Reader/Writer."},
    //{"read",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "read Block Data from authentication-required Service."},
    //{"write",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "write Block Data to an authentication-required Service."},
    //{"searchSvCodeV2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "verify the existence of Area or Service, and to acquire Key Version."},
    //{"getSysStatus",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "acquire the setup information in System."},
    //{"rqSpecVer",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "acquire the version of card OS."},
    //{"resetMode",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "reset Mode to Mode 0."},
    //{"auth1V2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "authenticate a card."},
    //{"auth2V2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "allow a card to authenticate a Reader/Writer."},
    //{"readV2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "read Block Data from authentication-required Service."},
    //{"writeV2",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "write Block Data to authentication-required Service."},
    //{"upRandomID",    CmdHFFelicaNotImplementedYet,    IfPm3Felica,     "update Random ID (IDr)."},
    {"----------- FeliCa Light -----------", CmdHelp,                IfPm3Iso14443a,  ""},
    {"litesim",   CmdHFFelicaSimLite,   IfPm3Felica,     "<NDEF2> - only reply to poll request"},
    {"litedump",  CmdHFFelicaDumpLite,  IfPm3Felica,     "Wait for and try dumping FelicaLite"},
    //    {"sim",       CmdHFFelicaSim,       IfPm3Felica,     "<UID> -- Simulate ISO 18092/FeliCa tag"}
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdHFFelica(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}
