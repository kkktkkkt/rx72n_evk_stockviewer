/* app_logfmt.c - 標準Cのみ。FITのヘッダを include しない。 */
#include <stdio.h>
#include <string.h>
#include "app_logfmt.h"

#define BUILD_ID_MAX  (31u)
#define SCAN_MAX      (4u)

/* snprintf の結果を「入りきった長さ」に正規化。失敗・切り詰めは buf を空にして 0 */
static size_t fin(char *buf, size_t size, int n)
{
    if ((n <= 0) || ((size_t)n >= size)) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n;
}

/* snprintf の戻り値 n を位置 *pos に加える。入りきらなければ false(size は buf 全体の大きさ) */
static bool adv(int n, size_t size, size_t *pos)
{
    if ((n < 0) || ((size_t)n >= (size - *pos))) {
        return false;
    }
    *pos += (size_t)n;
    return true;
}

/* 共通の引数検査。使える buf なら先頭を空にする。使えなければ false */
static bool arg_ok(char *buf, size_t size)
{
    if ((buf == NULL) || (size == 0u)) {
        return false;
    }
    buf[0] = '\0';
    return true;
}

size_t applog_fmt_boot(char *buf, size_t size, const char *build, uint8_t ch, uint32_t baud)
{
    size_t   pos = 0u;
    size_t   i   = 0u;
    unsigned c;

    if (!arg_ok(buf, size)) {
        return 0u;
    }
    if (build == NULL) {
        build = "unknown";
    }
    if (!adv(snprintf(buf, size, "[boot] fw=rx72n_gcc build="), size, &pos)) {
        return fin(buf, size, 0);
    }
    /* build の文字を1つずつ書く(局所バッファを持たない)。空白など範囲外は '_' */
    while ((build[i] != '\0') && (i < BUILD_ID_MAX)) {
        if ((pos + 1u) >= size) {
            return fin(buf, size, 0);
        }
        c = (unsigned)(unsigned char)build[i];
        buf[pos] = ((c >= 0x21u) && (c <= 0x7Eu)) ? (char)c : '_';
        pos++;
        i++;
    }
    buf[pos] = '\0';
    if (!adv(snprintf(buf + pos, size - pos, " ch=%u baud=%lu\r\n", (unsigned)ch, (unsigned long)baud),
             size, &pos)) {
        return fin(buf, size, 0);
    }
    return pos;
}

size_t applog_fmt_init(char *buf, size_t size, const char *stage, bool ok, uint32_t err, uint32_t detail)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[init] stage=%s ok=%u err=%lu detail=%lu\r\n",
                                   (stage != NULL) ? stage : "?", ok ? 1u : 0u,
                                   (unsigned long)err, (unsigned long)detail));
}

size_t applog_fmt_state(char *buf, size_t size, const char *tag, const applog_state_t *s, bool with_range)
{
    size_t pos = 0u;

    if (!arg_ok(buf, size) || (s == NULL)) {
        return 0u;
    }
    /* 引数が多いとスタックを使う(RXは r1〜r4 以外をスタックで渡す)ので、値3個ずつに分けて書く */
    if (!adv(snprintf(buf, size, "[%s] tick=%lu scr=%u", (tag != NULL) ? tag : "?",
                      (unsigned long)s->tick, (unsigned)s->scr), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (!adv(snprintf(buf + pos, size - pos, " demo=%u frm=%lu uf=%lu",
                      (unsigned)s->demo, (unsigned long)s->frames, (unsigned long)s->uf), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (!adv(snprintf(buf + pos, size - pos, " err=%lu detail=%lu pts=%u",
                      (unsigned long)s->err_code, (unsigned long)s->err_detail,
                      (unsigned)s->raw_points), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (!adv(snprintf(buf + pos, size - pos, " x=%u y=%u i2c_fail=%lu",
                      (unsigned)s->raw_x, (unsigned)s->raw_y, (unsigned long)s->i2c_fail_total),
             size, &pos)) {
        return fin(buf, size, 0);
    }
    if (!adv(snprintf(buf + pos, size - pos, " streak=%u found=%u drop=%lu",
                      (unsigned)s->i2c_fail_streak, (unsigned)s->found_count, (unsigned long)s->drop),
             size, &pos)) {
        return fin(buf, size, 0);
    }
    if (with_range) {
        if (!adv(snprintf(buf + pos, size - pos, " xmin=%u xmax=%u",
                          (unsigned)s->x_min, (unsigned)s->x_max), size, &pos)) {
            return fin(buf, size, 0);
        }
        if (!adv(snprintf(buf + pos, size - pos, " ymin=%u ymax=%u",
                          (unsigned)s->y_min, (unsigned)s->y_max), size, &pos)) {
            return fin(buf, size, 0);
        }
    }
    if ((pos + 2u) >= size) {
        return fin(buf, size, 0);
    }
    buf[pos]      = '\r';
    buf[pos + 1u] = '\n';
    buf[pos + 2u] = '\0';
    return pos + 2u;
}

size_t applog_fmt_scan(char *buf, size_t size, uint8_t count, const uint8_t *addr)
{
    size_t pos;
    int    n;
    uint8_t i;

    if (!arg_ok(buf, size)) {
        return 0u;
    }
    if (count > SCAN_MAX) {
        count = (uint8_t)SCAN_MAX;
    }
    if ((count > 0u) && (addr == NULL)) {
        return 0u;
    }
    n = snprintf(buf, size, "[scan] n=%u", (unsigned)count);
    if ((n <= 0) || ((size_t)n >= size)) {
        return fin(buf, size, 0);
    }
    pos = (size_t)n;
    for (i = 0u; i < count; i++) {
        n = snprintf(buf + pos, size - pos, " a%u=0x%02x", (unsigned)i, (unsigned)addr[i]);
        if ((n <= 0) || ((size_t)n >= (size - pos))) {
            return fin(buf, size, 0);
        }
        pos += (size_t)n;
    }
    if (pos + 2u >= size) {
        return fin(buf, size, 0);
    }
    buf[pos]      = '\r';
    buf[pos + 1u] = '\n';
    buf[pos + 2u] = '\0';
    return pos + 2u;
}

size_t applog_fmt_ev_tap(char *buf, size_t size, const char *src)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[ev] btn=tap src=%s\r\n", (src != NULL) ? src : "?"));
}

size_t applog_fmt_perf(char *buf, size_t size, uint32_t n, uint32_t copy_us, uint32_t home_us, uint32_t slide_us)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[perf] n=%lu copy_us=%lu home_us=%lu slide_us=%lu\r\n",
                                   (unsigned long)n, (unsigned long)copy_us, (unsigned long)home_us,
                                   (unsigned long)slide_us));
}

size_t applog_fmt_scr(char *buf, size_t size, bool to_clock, uint8_t pal, uint16_t clk_min, uint8_t kind, bool have, int8_t temp, uint32_t tick)
{
    static const char *const names[8] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown" };
    size_t pos = 0u;

    if (!arg_ok(buf, size)) {
        return 0u;
    }
    if ((pal > 8u) || (have && ((kind > 7u) || (temp < -99) || (temp > 99)))) {
        return 0u;
    }
    if (!adv(snprintf(buf, size, "[scr] to=%s pal=%u", to_clock ? "clock" : "stock", to_clock ? (unsigned)pal : 0u), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (clk_min > 1439u) {
        if (!adv(snprintf(buf + pos, size - pos, " clk=none"), size, &pos)) {
            return fin(buf, size, 0);
        }
    } else if (!adv(snprintf(buf + pos, size - pos, " clk=%02u%02u", (unsigned)(clk_min / 60u), (unsigned)(clk_min % 60u)), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (have) {
        if (!adv(snprintf(buf + pos, size - pos, " kind=%s temp=%d", names[kind], (int)temp), size, &pos)) {
            return fin(buf, size, 0);
        }
    } else if (!adv(snprintf(buf + pos, size - pos, " kind=none temp=none"), size, &pos)) {
        return fin(buf, size, 0);
    }
    if (!adv(snprintf(buf + pos, size - pos, " tick=%lu\r\n", (unsigned long)tick), size, &pos)) {
        return fin(buf, size, 0);
    }
    return pos;
}

size_t applog_fmt_bl(char *buf, size_t size, const char *mode, const char *cause, uint8_t pct, uint16_t clk_min, uint32_t tick)
{
    static const char *const modes[3]  = { "on", "dim", "off" };
    static const char *const causes[7] = { "boot", "night", "day", "touch", "timeout", "uart", "state" };
    unsigned i, mi = 3u, ci = 7u;
    char clk[8];

    if (!arg_ok(buf, size)) {
        return 0u;
    }
    if ((mode == NULL) || (cause == NULL)) {
        return fin(buf, size, 0);
    }
    for (i = 0u; i < 3u; i++) { if (strcmp(mode, modes[i]) == 0) { mi = i; } }
    for (i = 0u; i < 7u; i++) { if (strcmp(cause, causes[i]) == 0) { ci = i; } }
    if ((mi > 2u) || (ci > 6u) || (pct > 100u) || ((mi == 0u) && (pct != 100u)) || ((mi == 2u) && (pct != 0u)) ||
        ((mi == 1u) && ((pct == 0u) || (pct == 100u)))) {
        return fin(buf, size, 0);
    }
    if (clk_min > 1439u) {
        strcpy(clk, "none");
    } else {
        (void)snprintf(clk, sizeof clk, "%02u%02u", (unsigned)(clk_min / 60u), (unsigned)(clk_min % 60u));
    }
    return fin(buf, size, snprintf(buf, size, "[bl] mode=%s cause=%s pct=%u clk=%s tick=%lu\r\n",
                                   modes[mi], causes[ci], (unsigned)pct, clk, (unsigned long)tick));
}

size_t applog_fmt_err(char *buf, size_t size, uint32_t code, uint32_t detail)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[err] code=%lu detail=%lu\r\n",
                                   (unsigned long)code, (unsigned long)detail));
}

size_t applog_fmt_help(char *buf, size_t size)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[help] s=state x=update ?=help\r\n"));
}

applog_cmd_t applog_cmd_parse(int ch)
{
    switch (ch) {
    case 'n': return APPLOG_CMD_NEXT;
    case 'p': return APPLOG_CMD_PREV;
    case 's': return APPLOG_CMD_STATE;
    case '?': return APPLOG_CMD_HELP;
    case 'd': return APPLOG_CMD_DEMO;
    case 'x': return APPLOG_CMD_WX;
    case 'b': return APPLOG_CMD_BL;
    default:  return APPLOG_CMD_NONE;
    }
}

int applog_due(uint32_t now, uint32_t last, uint32_t period)
{
    return ((uint32_t)(now - last) >= period) ? 1 : 0;
}

int applog_err_changed(uint32_t code, uint32_t detail, uint32_t last_code, uint32_t last_detail)
{
    return ((code != last_code) || (detail != last_detail)) ? 1 : 0;
}
