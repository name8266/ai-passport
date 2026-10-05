#include "stock_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char symbol[9];
    assert(stock_symbol("600519", symbol) && !strcmp(symbol, "sh600519"));
    assert(stock_symbol("000001", symbol) && !strcmp(symbol, "sz000001"));
    assert(stock_symbol("920001", symbol) && !strcmp(symbol, "bj920001"));
    assert(!stock_symbol("000000", symbol));
    assert(!stock_symbol("12a456", symbol));
    assert(!stock_symbol("1234567", symbol));
    int32_t v;
    assert(stock_decimal("1258.620", &v) && v == 1258620);
    assert(stock_decimal("-0.25", &v) && v == -250);
    assert(!stock_decimal("NaN", &v));
    assert(!stock_decimal("9999999999999999999", &v));
    assert(!stock_decimal("10x", &v));
    assert(!stock_decimal(".5", &v));
    stock_wheel_t wheel;
    stock_wheel_init(&wheel, "000001");
    stock_wheel_turn(&wheel, -1);
    assert(!strcmp(wheel.code, "900001"));
    stock_wheel_turn(&wheel, 1);
    assert(!strcmp(wheel.code, "000001"));
    for (int i = 0; i < 5; i++)
        assert(!stock_wheel_next(&wheel));
    assert(stock_wheel_next(&wheel));
    stock_wheel_turn(&wheel, 1);
    assert(!strcmp(wheel.code, "000002"));
    assert(wheel.digit == 5);
    stock_watch_t w = {0};
    assert(stock_watch_add(&w, "600519"));
    assert(!stock_watch_add(&w, "600519"));
    assert(stock_watch_add(&w, "000001"));
    assert(stock_watch_remove(&w, "600519"));
    assert(w.count == 1 && !strcmp(w.codes[0], "000001"));
    assert(!stock_watch_remove(&w, "600519"));
    for (int i = 1; i < 12; i++) {
        char c[7];
        snprintf(c, sizeof(c), "600%03d", i);
        assert(stock_watch_add(&w, c));
    }
    assert(w.count == 12);
    assert(!stock_watch_add(&w, "600999"));
    stock_watch_t restored = {0};
    assert(stock_watch_decode(&restored, &w, sizeof(w)));
    assert(!memcmp(&w, &restored, sizeof(w)));
    assert(!stock_watch_decode(&restored, &w, sizeof(w) - 1));
    w.count = 13;
    assert(!stock_watch_decode(&restored, &w, sizeof(w)));
    w.count = 2;
    memcpy(w.codes[1], w.codes[0], 7);
    assert(!stock_watch_decode(&restored, &w, sizeof(w)));
    /* Tencent field positions, legacy GBK name, and a different symbol first. */
    char body[1800] =
        "v_sh600000=\"\";\nv_sz000001=\"51~\xc6\xbd\xb0\xb2\xd2\xf8\xd0\xd0~000001~11.57~11.35";
    for (int i = 5; i < 30; i++) {
        strcat(body, "~");
    }
    strcat(body, "~20260930161500~0.22~1.94\";");
    stock_quote_t q = {0};
    assert(stock_parse_quote(body, "000001", &q));
    assert(!strcmp(q.name, "平安银行"));
    assert(q.price == 11570 && q.previous == 11350 && q.change_bp == 194);
    assert(!strcmp(q.stamp, "20260930161500"));
    assert(!stock_parse_quote(body, "600519", &q));
    assert(!stock_parse_quote("v_sz000001=\"\";", "000001", &q));
    char name[5];
    stock_gbk_name("\xc6\xbd\xb0\xb2", 4, name, sizeof(name));
    assert(!strcmp(name, "平"));
    stock_gbk_name("\xff", 1, name, sizeof(name));
    assert(!strcmp(name, "?"));
    stock_bar_t bar;
    assert(stock_parse_bar("2026-09-30", "10.0", "10.2", "11.0", "9.5", &bar));
    assert(bar.close == 10200);
    assert(!stock_parse_bar("2026-09-30", "12", "10", "11", "9", &bar));
    assert(!stock_parse_bar("bad", "10", "10", "11", "9", &bar));
    assert(!stock_parse_bar("2026-09-30", "10", "10", "9", "11", &bar));
    uint64_t volume;
    assert(stock_volume("12345.67", &volume) && volume == 12345);
    assert(stock_volume("18446744073709551615", &volume) && volume == UINT64_MAX);
    assert(!stock_volume("18446744073709551616", &volume));
    assert(!stock_volume("-1", &volume));
    assert(!stock_volume("1.", &volume));
    stock_minute_t minute;
    assert(stock_parse_minute("0930 11.50 1234", &minute) && minute.hhmm == 930 &&
           minute.price == 11500);
    assert(stock_parse_minute("1130 11.50 1234", &minute));
    assert(stock_parse_minute("1300 11.50 1234", &minute));
    assert(stock_parse_minute("1500 11.50 1234", &minute));
    assert(!stock_parse_minute("1200 11.50 1234", &minute));
    assert(!stock_parse_minute("0960 11.50 1234", &minute));
    assert(!stock_parse_minute("1501 11.50 1234", &minute));
    assert(!stock_parse_minute("0930 0 1234", &minute));
    stock_bar_t series[20] = {0};
    for (unsigned i = 0; i < 20; i++)
        series[i].close = (i + 1) * 1000;
    assert(stock_ma(series, 20, 4, 5) == 3000);
    assert(stock_ma(series, 20, 19, 10) == 15500);
    assert(stock_ma(series, 20, 19, 20) == 10500);
    assert(stock_ma(series, 20, 3, 5) == 0);
    stock_config_t config = {.prefs = {.version = 1, .power_save = 1, .sound = 1}};
    assert(stock_watch_add(&config.watch, "600519"));
    assert(stock_watch_add(&config.watch, "000001"));
    assert(stock_watch_add(&config.watch, "600000"));
    stock_quote_t sorted[STOCK_MAX] = {{.valid = true, .change_bp = -100},
                                       {.valid = true, .change_bp = 200}};
    uint8_t order[STOCK_MAX];
    stock_order(&config.watch, sorted, STOCK_SORT_MANUAL, order);
    assert(order[0] == 0 && order[1] == 1);
    stock_order(&config.watch, sorted, STOCK_SORT_CODE, order);
    assert(order[0] == 1 && order[1] == 2 && order[2] == 0);
    stock_order(&config.watch, sorted, STOCK_SORT_CHANGE, order);
    assert(order[0] == 1 && order[1] == 0 && order[2] == 2);
    stock_alert_t alert = {.code = "000001", .kind = STOCK_PRICE_ABOVE, .target = 11000};
    q = (stock_quote_t){.code = "000001",
                        .stamp = "20260930150000",
                        .valid = true,
                        .price = 11500,
                        .change_bp = 200};
    assert(!stock_alert_evaluate(&alert, &q, false));
    assert(stock_alert_evaluate(&alert, &q, true));
    assert(alert.fired_date == 20260930);
    assert(!stock_alert_evaluate(&alert, &q, true));
    strcpy(q.stamp, "20261009150000");
    assert(stock_alert_evaluate(&alert, &q, true));
    alert.kind = STOCK_PERCENT_BELOW;
    alert.target = 100;
    alert.fired_date = 0;
    assert(!stock_alert_evaluate(&alert, &q, true));
    q.change_bp = -100;
    assert(stock_alert_evaluate(&alert, &q, true));
    alert.kind = STOCK_PRICE_BELOW;
    alert.target = 12000;
    alert.fired_date = 0;
    assert(stock_alert_evaluate(&alert, &q, true));
    alert.kind = STOCK_PERCENT_ABOVE;
    alert.target = 100;
    alert.fired_date = 0;
    q.change_bp = 100;
    assert(stock_alert_evaluate(&alert, &q, true));
    config.alerts[0] = alert;
    assert(stock_config_valid(&config));
    config.alerts[1] = alert;
    assert(!stock_config_valid(&config));
    config.alerts[1].kind = STOCK_ALERT_OFF;
    config.prefs.sort = 3;
    assert(!stock_config_valid(&config));
    assert(stock_refresh_seconds(true, 60, false) == 120);
    assert(stock_refresh_seconds(true, 60, true) == 30);
    assert(stock_refresh_seconds(true, 59, false) == 30);
    assert(stock_refresh_seconds(false, 100, false) == 30);
    puts("Stock core: PASS (markets, fixed-point values, wheel, watchlist persistence, GBK, "
         "quotes, OHLC, volume, sessions, MA, sorting, alerts, configuration, power)");
    return 0;
}
