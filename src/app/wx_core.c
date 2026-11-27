/* wx_core.c - 天気・気温の取得と時刻同期の論理部。標準Cのみ。FITのヘッダを include しない。
 * 仕様: docs/design.md §22.2〜§22.7、docs/design-uart.md §13。
 * 応答の本文は解析して数値だけを取り出す。ログ関数(wx_fmt_*)は本文・SSID・パスワードを引数に取らない。 */
#include <stdio.h>
#include <string.h>
#include "wx_core.h"
#include "wifi_sw.h"

#if APP_WIFI_ON   /* 0 の時は中身を消す(wifi_sw.h) */

#define JST_OFFSET_S   (32400u)    /* +9 時間 */
#define TEMP_LIM_X10   (600u)      /* ±60.0℃ */

static const char *const k_kind_name[WX_KIND_END] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown" };
static const char *const k_src_name[WX_SRC_END]   = { "boot", "timer", "touch", "uart" };
static const char *const k_net_name[4]            = { "off", "conn", "on", "err" };
static const char *const k_req_name[4]            = { "ok", "ignored", "busy", "off" };
static const char *const k_month[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* snprintf の結果を「入りきった長さ」に正規化。失敗・切り詰めは buf を空にして 0 */
static size_t fin(char *buf, size_t size, int n)
{
    if ((n <= 0) || ((size_t)n >= size)) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n;
}

static bool arg_ok(char *buf, size_t size)
{
    if ((buf == NULL) || (size == 0u)) {
        return false;
    }
    buf[0] = '\0';
    return true;
}

/* 接続先の表(wx_target_t の並び)。要求の Host も同じ host を使う */
typedef struct { const char *type; const char *host; uint16_t port; } tgt_t;
static const tgt_t k_tgt[WX_TGT_END] = {
    { "TCP", "api.open-meteo.com",         80u  },
    { "TCP", "query1.finance.yahoo.com",   80u  },
    { "SSL", "query1.finance.yahoo.com",   443u },
    { "SSL", "www.google.com",             443u },
    { "SSL", "query1.finance.yahoo.com",   443u }
};
static const char *const k_sym_enc[3] = { "6723.T", "%5EN225", "%5EGSPC" };   /* ^ は %5E */

size_t wx_build_cipstart(char *buf, size_t size, wx_target_t t)
{
    if (!arg_ok(buf, size) || ((unsigned)t >= (unsigned)WX_TGT_END)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "AT+CIPSTART=\"%s\",\"%s\",%u\r\n", k_tgt[t].type, k_tgt[t].host, (unsigned)k_tgt[t].port));
}

bool wx_target_ssl(wx_target_t t)
{
    return ((unsigned)t < (unsigned)WX_TGT_END) && (k_tgt[t].type[0] == 'S');
}

/* 天気の要求は固定文字列。外部の入力を含めない(190 バイト) */
#define WX_REQUEST \
    "GET /v1/forecast?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%2FTokyo HTTP/1.0\r\n" \
    "Host: api.open-meteo.com\r\nUser-Agent: rx72n-demo/1\r\nConnection: close\r\n\r\n"

size_t wx_build_request(char *buf, size_t size, wx_target_t t, uint8_t sym)
{
    if (!arg_ok(buf, size) || ((unsigned)t >= (unsigned)WX_TGT_END)) {
        return 0u;
    }
    if (t == WX_TGT_METEO) {
        if (size < (sizeof(WX_REQUEST))) {        /* sizeof = 長さ + NUL */
            return 0u;
        }
        memcpy(buf, WX_REQUEST, sizeof(WX_REQUEST));
        return sizeof(WX_REQUEST) - 1u;
    }
    if (t == WX_TGT_GOOGLE_SSL) {                 /* TLS が通るかだけを見る対照(design.md §5.5.2 の g) */
        return fin(buf, size, snprintf(buf, size, "GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: " WX_UA "\r\nConnection: close\r\n\r\n", k_tgt[t].host));
    }
    if (sym >= 3u) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "GET /v8/finance/chart/%s?range=" WX_RANGE_STR "&interval=1d HTTP/1.0\r\nHost: %s\r\n"
                                   "User-Agent: " WX_UA "\r\nConnection: close\r\n\r\n", k_sym_enc[sym], k_tgt[t].host));
}

const char *wx_kind_name(uint8_t kind)
{
    return (kind < (uint8_t)WX_KIND_END) ? k_kind_name[kind] : NULL;
}

/* WMO コード → アイコン(design.md §22.5 の表) */
uint8_t wx_kind_of(uint32_t w)
{
    if (w <= 1u) {
        return (uint8_t)WX_KIND_SUN;
    }
    if (w == 2u) {
        return (uint8_t)WX_KIND_PARTLY;
    }
    if (w == 3u) {
        return (uint8_t)WX_KIND_CLOUD;
    }
    if ((w == 45u) || (w == 48u)) {
        return (uint8_t)WX_KIND_FOG;
    }
    /* 表にあるコードだけ(52・54・62・64・72・74 などの未定義は UNKNOWN) */
    if ((w == 51u) || (w == 53u) || (w == 55u) || (w == 56u) || (w == 57u) || (w == 61u) || (w == 63u) || (w == 65u) || (w == 66u) || (w == 67u)
        || ((w >= 80u) && (w <= 82u))) {
        return (uint8_t)WX_KIND_RAIN;
    }
    if ((w == 71u) || (w == 73u) || (w == 75u) || (w == 77u) || (w == 85u) || (w == 86u)) {
        return (uint8_t)WX_KIND_SNOW;
    }
    if ((w == 95u) || (w == 96u) || (w == 99u)) {
        return (uint8_t)WX_KIND_THUNDER;
    }
    return (uint8_t)WX_KIND_UNKNOWN;
}

/* ================================================================ 日付 */

/* Howard Hinnant の days_from_civil(1970-01-01 = 0)。y >= 1970 */
static uint32_t days_from_civil(uint32_t y, uint32_t m, uint32_t d)
{
    uint32_t era;
    uint32_t yoe;
    uint32_t doy;
    uint32_t doe;

    if (m <= 2u) {
        y--;
    }
    era = y / 400u;
    yoe = y - (era * 400u);
    doy = (((153u * ((m > 2u) ? (m - 3u) : (m + 9u))) + 2u) / 5u) + d - 1u;
    doe = (yoe * 365u) + (yoe / 4u) - (yoe / 100u) + doy;
    return (era * 146097u) + doe - 719468u;
}

static bool is_leap(uint32_t y)
{
    return (((y % 4u) == 0u) && ((y % 100u) != 0u)) || ((y % 400u) == 0u);
}

static uint32_t days_in_month(uint32_t y, uint32_t m)
{
    static const uint8_t k[12] = { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };

    return ((m == 2u) && is_leap(y)) ? 29u : (uint32_t)k[m - 1u];
}

void wx_date_fields(uint32_t secs, wx_date_t *out)
{
    uint32_t days = secs / 86400u;
    uint32_t rem  = secs % 86400u;
    uint32_t z    = days + 719468u;
    uint32_t era  = z / 146097u;
    uint32_t doe  = z - (era * 146097u);
    uint32_t yoe  = (doe - (doe / 1460u) + (doe / 36524u) - (doe / 146096u)) / 365u;
    uint32_t y    = yoe + (era * 400u);
    uint32_t doy  = doe - ((365u * yoe) + (yoe / 4u) - (yoe / 100u));
    uint32_t mp   = ((5u * doy) + 2u) / 153u;
    uint32_t d    = doy - (((153u * mp) + 2u) / 5u) + 1u;
    uint32_t m    = (mp < 10u) ? (mp + 3u) : (mp - 9u);

    if (m <= 2u) {
        y++;
    }
    out->year  = (uint16_t)y;
    out->month = (uint8_t)m;
    out->day   = (uint8_t)d;
    out->wday  = (uint8_t)((days + 4u) % 7u);        /* 1970-01-01 = 木(4) */
    out->hour  = (uint8_t)(rem / 3600u);
    out->min   = (uint8_t)((rem % 3600u) / 60u);
    out->sec   = (uint8_t)(rem % 60u);
}

static bool is_dig(char c)
{
    return (c >= '0') && (c <= '9');
}

/* p から最大 maxd 桁(最小 mind 桁)の 10 進数。p は進む。end を越えない */
static bool rd_uint(const char **p, const char *end, unsigned mind, unsigned maxd, uint32_t *v)
{
    unsigned n = 0u;
    uint32_t x = 0u;

    while ((*p < end) && is_dig(**p) && (n < maxd)) {
        x = (x * 10u) + (uint32_t)(**p - '0');
        (*p)++;
        n++;
    }
    if (n < mind) {
        return false;
    }
    *v = x;
    return true;
}

static bool rd_ch(const char **p, const char *end, char c)
{
    if ((*p < end) && (**p == c)) {
        (*p)++;
        return true;
    }
    return false;
}

static void skip_sp(const char **p, const char *end)
{
    while ((*p < end) && ((**p == ' ') || (**p == '\t'))) {
        (*p)++;
    }
}

bool wx_parse_date(const char *s, size_t len, uint32_t *utc_s)
{
    const char *p;
    const char *end;
    uint32_t    d, y, hh, mm, ss, m;
    unsigned    i;

    if ((s == NULL) || (utc_s == NULL)) {
        return false;
    }
    p   = s;
    end = s + len;
    skip_sp(&p, end);
    for (i = 0u; i < 3u; i++) {                         /* 曜日(3 文字。値は使わない) */
        if ((p >= end) || !(((*p >= 'A') && (*p <= 'Z')) || ((*p >= 'a') && (*p <= 'z')))) {
            return false;
        }
        p++;
    }
    if (!rd_ch(&p, end, ',')) {
        return false;
    }
    skip_sp(&p, end);
    if (!rd_uint(&p, end, 1u, 2u, &d)) {
        return false;
    }
    skip_sp(&p, end);
    if ((end - p) < 3) {
        return false;
    }
    for (m = 0u; m < 12u; m++) {
        if (memcmp(p, k_month[m], 3u) == 0) {
            break;
        }
    }
    if (m >= 12u) {
        return false;
    }
    m++;
    p += 3;
    skip_sp(&p, end);
    if (!rd_uint(&p, end, 4u, 4u, &y) || (p >= end) || (*p != ' ')) {
        return false;
    }
    skip_sp(&p, end);
    if (!rd_uint(&p, end, 2u, 2u, &hh) || !rd_ch(&p, end, ':') || !rd_uint(&p, end, 2u, 2u, &mm) || !rd_ch(&p, end, ':')
        || !rd_uint(&p, end, 2u, 2u, &ss)) {
        return false;
    }
    skip_sp(&p, end);
    if (((end - p) < 3) || (memcmp(p, "GMT", 3u) != 0)) {
        return false;
    }
    if ((y < 2020u) || (y > 2099u) || (hh > 23u) || (mm > 59u) || (ss > 59u) || (d < 1u) || (d > days_in_month(y, m))) {
        return false;
    }
    *utc_s = (days_from_civil(y, m, d) * 86400u) + (hh * 3600u) + (mm * 60u) + ss;
    return true;
}

/* ================================================================ HTTP の解析 */

/* hay[0..hn) の中から needle(NUL 終端)を探す。見つかったらその次の位置、無ければ NULL */
static const char *find_after(const char *hay, size_t hn, const char *needle)
{
    size_t nl = strlen(needle);
    size_t i;

    if ((nl == 0u) || (hn < nl)) {
        return NULL;
    }
    for (i = 0u; (i + nl) <= hn; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            return hay + i + nl;
        }
    }
    return NULL;
}

static char lower(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? (char)(c + 0x20) : c;
}

/* ヘッダー部 [h, h + hn) の行頭の Date: を読む(大文字小文字を区別しない) */
static bool find_date(const char *h, size_t hn, uint32_t *utc_s)
{
    static const char key[5] = { 'd', 'a', 't', 'e', ':' };
    size_t i = 0u;
    size_t j;
    size_t k;

    while (i < hn) {
        if (((hn - i) >= 5u) && ((i == 0u) || (h[i - 1u] == '\n'))) {
            for (k = 0u; (k < 5u) && (lower(h[i + k]) == key[k]); k++) {
                /* 一致を数える */
            }
            if (k == 5u) {
                for (j = i + 5u; (j < hn) && (h[j] != '\r') && (h[j] != '\n'); j++) {
                    /* 行末まで */
                }
                return wx_parse_date(h + i + 5u, j - (i + 5u), utc_s);
            }
        }
        i++;
    }
    return false;
}

static bool is_term(char c)
{
    return (c == ',') || (c == '}') || (c == ' ') || (c == '\r') || (c == '\n');
}

/* 気温。0 = 成功、5 = 無い・読めない・途切れ、6 = 範囲外 */
static uint8_t read_temp(const char *p, const char *end, int16_t *out)
{
    bool     neg = false;
    unsigned id  = 0u;
    unsigned fd  = 0u;
    uint32_t iv  = 0u;
    uint32_t fv  = 0u;
    uint32_t t10;

    if ((p < end) && (*p == '-')) {
        neg = true;
        p++;
    }
    while ((p < end) && is_dig(*p)) {
        if (iv < 1000u) {
            iv = (iv * 10u) + (uint32_t)(*p - '0');
        }
        id++;
        p++;
    }
    if ((p < end) && (*p == '.')) {
        p++;
        while ((p < end) && is_dig(*p)) {
            if (fd == 0u) {
                fv = (uint32_t)(*p - '0');                 /* 2 桁目以降は切り捨て */
            }
            fd++;
            p++;
        }
    }
    if (((id + fd) == 0u) || (p >= end) || !is_term(*p)) {
        return 5u;                                          /* null・文字列・数字なし・途切れ・指数表記 */
    }
    t10 = (iv * 10u) + fv;
    if ((id > 2u) || (t10 > TEMP_LIM_X10)) {
        return 6u;
    }
    *out = neg ? (int16_t)(-(int32_t)t10) : (int16_t)t10;
    return 0u;
}

/* 天気コード。0 = 成功、5 = 途切れ、6 = null・負・100 以上 */
static uint8_t read_code(const char *p, const char *end, uint8_t *out)
{
    bool     neg = false;
    unsigned n   = 0u;
    uint32_t v   = 0u;

    if ((p < end) && (*p == '-')) {
        neg = true;
        p++;
    }
    while ((p < end) && is_dig(*p)) {
        if (v < 1000u) {
            v = (v * 10u) + (uint32_t)(*p - '0');
        }
        n++;
        p++;
    }
    if (n == 0u) {
        return ((end - p) < 4) ? 5u : 6u;                   /* null 等は範囲外。4 文字に満たない = 途切れ */
    }
    if (p >= end) {
        return 5u;
    }
    if (neg || (v > 99u) || !is_term(*p)) {
        return 6u;
    }
    *out = (uint8_t)v;
    return 0u;
}

bool wx_parse_http(const char *buf, size_t len, wx_result_t *out)
{
    const char *end;
    const char *body;
    const char *cur;
    const char *v;
    size_t      hn;
    uint32_t    utc;
    uint32_t    http;

    out->ok        = 0u;
    out->err       = 0u;
    out->have_date = 0u;
    out->wmo       = 0u;
    out->kind      = (uint8_t)WX_KIND_UNKNOWN;
    out->temp_x10  = 0;
    out->http      = 0u;
    out->obs       = 0u;
    out->jst_s     = 0u;
    if (buf == NULL) {
        out->err = 3u;
        return false;
    }
    end = buf + len;
    if (len < 12u) {
        out->err = 3u;                                      /* 状態行まで届かない */
        return false;
    }
    if ((memcmp(buf, "HTTP/1.", 7u) != 0) || ((buf[7] != '0') && (buf[7] != '1')) || (buf[8] != ' ')
        || !is_dig(buf[9]) || !is_dig(buf[10]) || !is_dig(buf[11])) {
        out->err = 4u;
        return false;
    }
    http = (uint32_t)((buf[9] - '0') * 100) + (uint32_t)((buf[10] - '0') * 10) + (uint32_t)(buf[11] - '0');
    out->http = (uint16_t)http;

    body = find_after(buf, len, "\r\n\r\n");
    hn   = (body != NULL) ? (size_t)((body - 4) - buf) : len;
    if (find_date(buf, hn, &utc)) {
        out->have_date = 1u;
        out->jst_s     = utc + JST_OFFSET_S;
    }
    if (body == NULL) {
        body = buf;
    }
    if (http != 200u) {
        out->err = 4u;
        return false;
    }
    cur = find_after(body, (size_t)(end - body), "\"current\":");          /* "current_units": は一致しない */
    if (cur == NULL) {
        out->err = 5u;
        return false;
    }
    v = find_after(cur, (size_t)(end - cur), "\"temperature_2m\":");
    if (v == NULL) {
        out->err = 5u;
        return false;
    }
    out->err = read_temp(v, end, &out->temp_x10);
    if (out->err != 0u) {
        return false;
    }
    v = find_after(cur, (size_t)(end - cur), "\"weather_code\":");
    if (v == NULL) {
        out->err = 5u;
        return false;
    }
    out->err = read_code(v, end, &out->wmo);
    if (out->err != 0u) {
        return false;
    }
    out->kind = wx_kind_of(out->wmo);
    v = find_after(cur, (size_t)(end - cur), "\"time\":\"");
    if ((v != NULL) && ((end - v) >= 16) && is_dig(v[0]) && is_dig(v[1]) && is_dig(v[2]) && is_dig(v[3]) && (v[4] == '-') && (v[7] == '-')
        && (v[10] == 'T') && is_dig(v[11]) && is_dig(v[12]) && (v[13] == ':') && is_dig(v[14]) && is_dig(v[15])) {
        out->obs = (uint16_t)((((v[11] - '0') * 10) + (v[12] - '0')) * 100 + ((v[14] - '0') * 10) + (v[15] - '0'));
    }
    out->ok = 1u;
    return true;
}

/* ================================================================ ソフト時計 */

void wx_clock_init(wx_clock_t *k)
{
    memset(k, 0, sizeof *k);
}

void wx_clock_sync(wx_clock_t *k, uint32_t jst_s, uint32_t first_ms)
{
    k->base_s  = jst_s;
    k->base_ms = first_ms - WX_SYNC_BIAS_MS;       /* uint32 の引き算(巻き戻っても差は正しい) */
    k->synced  = 1u;
}

bool wx_clock_now(const wx_clock_t *k, uint32_t now_ms, uint32_t *jst_s)
{
    if (k->synced == 0u) {
        return false;
    }
    *jst_s = k->base_s + ((uint32_t)(now_ms - k->base_ms) / 1000u);
    return true;
}

uint16_t wx_clock_min(const wx_clock_t *k, uint32_t now_ms)
{
    uint32_t s;

    if (!wx_clock_now(k, now_ms, &s)) {
        return 0xFFFFu;
    }
    return (uint16_t)((s % 86400u) / 60u);
}

/* ================================================================ ログ行 */

size_t wx_fmt_ok(char *buf, size_t size, wx_src_t src, const wx_result_t *r)
{
    wx_date_t d;
    char      jst[8];

    if (!arg_ok(buf, size) || ((unsigned)src >= (unsigned)WX_SRC_END) || (r == NULL) || (r->kind >= (uint8_t)WX_KIND_END)
        || (r->temp_x10 < -(int16_t)TEMP_LIM_X10) || (r->temp_x10 > (int16_t)TEMP_LIM_X10) || (r->wmo > 99u) || (r->http > 999u) || (r->obs > 9999u)) {
        return 0u;
    }
    if (r->have_date != 0u) {
        wx_date_fields(r->jst_s, &d);
        (void)snprintf(jst, sizeof jst, "%02u%02u%02u", (unsigned)(d.hour % 100u), (unsigned)(d.min % 100u), (unsigned)(d.sec % 100u));
    } else {
        (void)snprintf(jst, sizeof jst, "none");
    }
    return fin(buf, size, snprintf(buf, size, "[wx] res=ok src=%s temp10=%d wmo=%u kind=%s http=%u ms=%lu sync=%u jst=%s obs=%04u closed=%u\r\n",
                                   k_src_name[src], (int)r->temp_x10, (unsigned)r->wmo, k_kind_name[r->kind], (unsigned)r->http,
                                   (unsigned long)r->ms, (unsigned)((r->have_date != 0u) ? 1u : 0u), jst, (unsigned)r->obs,
                                   (unsigned)((r->closed != 0u) ? 1u : 0u)));
}

size_t wx_fmt_fail(char *buf, size_t size, wx_src_t src, const wx_result_t *r)
{
    if (!arg_ok(buf, size) || ((unsigned)src >= (unsigned)WX_SRC_END) || (r == NULL) || (r->err < 1u) || (r->err > 9u) || (r->http > 999u)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[wx] res=fail src=%s err=%u http=%u ms=%lu len=%u\r\n",
                                   k_src_name[src], (unsigned)r->err, (unsigned)r->http, (unsigned long)r->ms, (unsigned)r->len));
}

size_t wx_fmt_req(char *buf, size_t size, wx_req_t req, wx_src_t src)
{
    if (!arg_ok(buf, size) || ((unsigned)req < 1u) || ((unsigned)req > 3u) || ((src != WX_SRC_TOUCH) && (src != WX_SRC_UART))) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[wx] req=%s src=%s\r\n", k_req_name[req], k_src_name[src]));
}

size_t wx_fmt_state(char *buf, size_t size, const wx_view_t *v)
{
    wx_date_t d;
    char      clk[8];
    char      date[12];
    unsigned  wd;
    long      age;
    int       n1;

    if (!arg_ok(buf, size) || (v == NULL) || (v->net > 3u) || ((v->have != 0u) && (v->kind >= (uint8_t)WX_KIND_END))
        || (v->temp_x10 < -(int16_t)TEMP_LIM_X10) || (v->temp_x10 > (int16_t)TEMP_LIM_X10)) {
        return 0u;
    }
    if (v->synced != 0u) {
        wx_date_fields(v->jst_s, &d);
        (void)snprintf(clk, sizeof clk, "%02u%02u%02u", (unsigned)(d.hour % 100u), (unsigned)(d.min % 100u), (unsigned)(d.sec % 100u));
        (void)snprintf(date, sizeof date, "%04u%02u%02u", (unsigned)(d.year % 10000u), (unsigned)(d.month % 100u), (unsigned)(d.day % 100u));
        wd = d.wday;
    } else {
        (void)snprintf(clk, sizeof clk, "none");
        (void)snprintf(date, sizeof date, "none");
        wd = 9u;
    }
    age = (v->age_s < 0) ? -1L : (long)((v->age_s > 99999999L) ? 99999999L : v->age_s);
    n1 = snprintf(buf, size, "[wx] net=%s busy=%u have=%u temp10=%d", k_net_name[v->net], (unsigned)((v->busy != 0u) ? 1u : 0u),
                  (unsigned)((v->have != 0u) ? 1u : 0u), (int)v->temp_x10);
    if ((n1 <= 0) || ((size_t)n1 >= size)) {
        buf[0] = '\0';
        return 0u;
    }
    return fin(buf, size, n1 + snprintf(buf + n1, size - (size_t)n1, " kind=%s age=%ld clk=%s date=%s wd=%u sync=%u\r\n",
                                        (v->have != 0u) ? k_kind_name[v->kind] : "none", age, clk, date, wd, (unsigned)((v->synced != 0u) ? 1u : 0u)));
}

/* ================================================================ 取得の予定表 */

void wx_sched_init(wx_sched_t *s, bool enabled, uint32_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->src = (uint8_t)WX_SRC_BOOT;
    if (enabled) {
        s->state = (uint8_t)WX_S_BOOT_WAIT;
        s->net   = (uint8_t)WX_NET_CONN;
        s->t_due = now_ms + WX_BOOT_WAIT_MS;
    } else {
        s->state = (uint8_t)WX_S_OFF;
        s->net   = (uint8_t)WX_NET_OFF;
    }
}

wx_act_t wx_sched_poll(wx_sched_t *s, uint32_t now_ms)
{
    switch (s->state) {
    case (uint8_t)WX_S_BOOT_WAIT:
        if ((uint32_t)(now_ms - s->t_due) < 0x80000000u) {      /* 期限を過ぎた(32bit の巻き戻りに対応) */
            s->state = (uint8_t)WX_S_CONNECT;
        }
        break;
    case (uint8_t)WX_S_IDLE:
        if ((uint32_t)(now_ms - s->t_fetch0) >= WX_INTERVAL_MS) {
            s->state = (uint8_t)WX_S_FETCH;
            s->src   = (uint8_t)WX_SRC_TIMER;
        }
        break;
    case (uint8_t)WX_S_RETRY:
        if ((uint32_t)(now_ms - s->t_due) < 0x80000000u) {
            s->state = (s->want == (uint8_t)WX_ACT_FETCH) ? (uint8_t)WX_S_FETCH : (uint8_t)WX_S_CONNECT;
            if (s->state == (uint8_t)WX_S_FETCH) {
                s->src = (uint8_t)WX_SRC_TIMER;                  /* やり直しは timer */
            }
        }
        break;
    default:
        break;
    }
    if (s->started != 0u) {
        return WX_ACT_NONE;
    }
    if (s->state == (uint8_t)WX_S_CONNECT) {
        s->started = 1u;
        s->net     = (uint8_t)WX_NET_CONN;
        return WX_ACT_CONNECT;
    }
    if (s->state == (uint8_t)WX_S_FETCH) {
        s->started  = 1u;
        s->t_fetch0 = now_ms;
        return WX_ACT_FETCH;
    }
    return WX_ACT_NONE;
}

static void retry_in(wx_sched_t *s, wx_act_t want, uint32_t wait_ms, uint32_t now_ms)
{
    s->state = (uint8_t)WX_S_RETRY;
    s->want  = (uint8_t)want;
    s->t_due = now_ms + wait_ms;
    s->net   = (uint8_t)WX_NET_ERR;
}

void wx_sched_done(wx_sched_t *s, wx_act_t act, bool ok, uint16_t http, uint32_t now_ms)
{
    static const uint32_t k_conn_wait[3] = { 60000u, 120000u, 300000u };

    if ((s->started == 0u) || ((act == WX_ACT_CONNECT) && (s->state != (uint8_t)WX_S_CONNECT))
        || ((act == WX_ACT_FETCH) && (s->state != (uint8_t)WX_S_FETCH)) || (act == WX_ACT_NONE)) {
        return;                                                  /* 始めていない動きの完了は無視 */
    }
    s->started = 0u;
    if (act == WX_ACT_CONNECT) {
        if (ok) {
            s->conn_fail = 0u;
            s->net       = (uint8_t)WX_NET_ON;
            s->state     = (uint8_t)WX_S_FETCH;                  /* 直ちに最初の取得 */
        } else {
            retry_in(s, WX_ACT_CONNECT, k_conn_wait[(s->conn_fail < 2u) ? s->conn_fail : 2u], now_ms);
            if (s->conn_fail < 2u) {
                s->conn_fail++;
            }
        }
        return;
    }
    if (ok) {
        s->fetch_fail = 0u;
        s->have       = 1u;
        s->net        = (uint8_t)WX_NET_ON;
        s->state      = (uint8_t)WX_S_IDLE;
    } else if (http == 429u) {
        retry_in(s, WX_ACT_FETCH, WX_RATE_MS, now_ms);
    } else {
        s->fetch_fail++;
        if (s->fetch_fail >= 3u) {                               /* 3 回目も失敗: ESP32 を再接続 */
            s->fetch_fail = 0u;
            s->net        = (uint8_t)WX_NET_ERR;
            s->state      = (uint8_t)WX_S_CONNECT;
        } else {
            retry_in(s, WX_ACT_FETCH, WX_RETRY_MS, now_ms);
        }
    }
}

wx_req_t wx_sched_request(wx_sched_t *s, wx_src_t src, uint32_t now_ms)
{
    switch (s->state) {
    case (uint8_t)WX_S_OFF:
        return WX_REQ_OFF;
    case (uint8_t)WX_S_IDLE:
        if ((uint32_t)(now_ms - s->t_fetch0) < WX_MIN_GAP_MS) {
            return WX_REQ_IGNORED;
        }
        s->state = (uint8_t)WX_S_FETCH;
        s->src   = (uint8_t)src;
        return WX_REQ_OK;
    case (uint8_t)WX_S_RETRY:
        s->state = (uint8_t)WX_S_CONNECT;                        /* エラー中は直ちに再接続 */
        s->src   = (uint8_t)src;
        return WX_REQ_OK;
    default:
        return WX_REQ_BUSY;                                      /* BOOT_WAIT / CONNECT / FETCH */
    }
}

wx_src_t wx_sched_src(const wx_sched_t *s)
{
    return (wx_src_t)s->src;
}

wx_net_t wx_sched_net(const wx_sched_t *s)
{
    return (wx_net_t)s->net;
}

bool wx_sched_busy(const wx_sched_t *s)
{
    return (s->state == (uint8_t)WX_S_CONNECT) || (s->state == (uint8_t)WX_S_FETCH);
}

#endif /* APP_WIFI_ON */
