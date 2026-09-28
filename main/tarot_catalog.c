#include "tarot_catalog.h"

#include <stdio.h>

extern const uint8_t tarot_cards_rgb565_start[] asm("_binary_tarot_cards_rgb565_start");
extern const uint8_t tarot_cards_rgb565_end[] asm("_binary_tarot_cards_rgb565_end");

static const char *const MAJOR_NAMES[] = {
    "愚者", "魔术师", "女祭司", "皇后", "皇帝", "教皇", "恋人", "战车",
    "力量", "隐士", "命运之轮", "正义", "倒吊人", "死神", "节制", "恶魔",
    "高塔", "星星", "月亮", "太阳", "审判", "世界",
};

static const char *const MAJOR_UP[] = {
    "新的旅程与信任", "行动与创造", "直觉与内在知识", "滋养与丰盛",
    "秩序与担当", "传统与学习", "选择与连结", "意志与推进",
    "温柔的勇气", "独处与求真", "转机与周期", "公平与责任",
    "暂停与换位思考", "结束与重生", "调和与耐心", "欲望与束缚",
    "突变与真相", "希望与疗愈", "迷雾与潜意识", "喜悦与清晰",
    "觉醒与回应", "完成与整合",
};

static const char *const MAJOR_REVERSED[] = {
    "冲动或迟疑", "能量分散", "忽略直觉", "过度付出或匮乏",
    "僵化或失控", "盲从或质疑", "失衡的选择", "方向失焦",
    "自我怀疑", "孤立或逃避", "抗拒变化", "偏见或逃责",
    "无谓拖延", "抗拒结束", "失衡与急躁", "看见并松开束缚",
    "延迟的震荡", "信心受阻", "焦虑与误判", "快乐被遮蔽",
    "回避召唤", "尚未收尾",
};

static const char *const SUITS[] = { "权杖", "圣杯", "宝剑", "星币" };
static const char *const SUIT_UP[] = {
    "行动、热情与创造力", "情感、关系与直觉", "思想、沟通与抉择", "资源、身体与长期建设",
};
static const char *const SUIT_REVERSED[] = {
    "行动受阻或热情失衡", "情绪淤积或关系失衡", "思绪冲突或沟通偏差", "现实压力或资源失衡",
};
static const char *const RANKS[] = {
    "一", "二", "三", "四", "五", "六", "七", "八", "九", "十", "侍从", "骑士", "王后", "国王",
};
static const char *const RANK_UP[] = {
    "种子正在萌发", "需要作出选择", "合作带来成长", "建立稳定边界", "正面对冲突与变化",
    "恢复流动与互助", "坚持立场并评估选项", "进入专注推进期", "成果临近也需守护自己",
    "一个周期达到饱和", "保持好奇并学习", "让动力找到方向", "以成熟感受滋养局面", "承担责任并稳健领导",
};
static const char *const RANK_REVERSED[] = {
    "起点尚未稳定", "选择被拖延", "协作出现错位", "边界过紧或松动", "冲突消耗了注意力",
    "付出与回收不均", "防御或幻想过多", "节奏受阻或过度用力", "压力需要释放",
    "负担超过承载", "消息尚不成熟", "行动急躁或停滞", "照顾他人前先照顾自己", "控制欲或责任缺位",
};

const char *tarot_card_name(uint8_t card_id) {
    static char names[56][16];
    static bool initialized;
    if (card_id >= 78) return "未知牌";
    if (card_id < 22) return MAJOR_NAMES[card_id];
    if (!initialized) {
        for (size_t i = 0; i < 56; ++i) {
            snprintf(names[i], sizeof(names[i]), "%s%s", SUITS[i / 14], RANKS[i % 14]);
        }
        initialized = true;
    }
    return names[card_id - 22];
}

const char *tarot_card_keyword(uint8_t card_id, bool reversed) {
    if (card_id >= 78) return "信息不可用";
    if (card_id < 22) return reversed ? MAJOR_REVERSED[card_id] : MAJOR_UP[card_id];
    size_t suit = (card_id - 22) / 14;
    return reversed ? SUIT_REVERSED[suit] : SUIT_UP[suit];
}

void tarot_card_interpret(uint8_t card_id, bool reversed, const char *position,
                          char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    if (!position) position = "当前";
    if (card_id < 22) {
        snprintf(buffer, buffer_size, "%s｜%s%s\n%s。把它当作观察视角，而不是不可改变的预言。",
                 position, tarot_card_name(card_id), reversed ? "（逆位）" : "（正位）",
                 tarot_card_keyword(card_id, reversed));
        return;
    }
    size_t offset = card_id - 22;
    size_t rank = offset % 14;
    const char *rank_text = reversed ? RANK_REVERSED[rank] : RANK_UP[rank];
    snprintf(buffer, buffer_size, "%s｜%s%s\n%s；%s。留意现实证据，再决定下一步。",
             position, tarot_card_name(card_id), reversed ? "（逆位）" : "（正位）",
             tarot_card_keyword(card_id, reversed), rank_text);
}

bool tarot_card_image(uint8_t card_id, lv_image_dsc_t *descriptor) {
    if (!descriptor || card_id >= 78) return false;
    size_t atlas_size = (size_t)(tarot_cards_rgb565_end - tarot_cards_rgb565_start);
    if (atlas_size != 78U * TAROT_IMAGE_BYTES) return false;
    *descriptor = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = TAROT_IMAGE_WIDTH,
            .h = TAROT_IMAGE_HEIGHT,
            .stride = TAROT_IMAGE_WIDTH * 2,
        },
        .data_size = TAROT_IMAGE_BYTES,
        .data = tarot_cards_rgb565_start + (size_t)card_id * TAROT_IMAGE_BYTES,
    };
    return true;
}
