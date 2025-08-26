/**
 * LVGL Configuration for USB Bridge
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_DEV_VERSION 0

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS (LV_COLOR_DEPTH == 32 ? 0: 128)
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*=================
   MEMORY SETTINGS
 *=================*/
#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    #define LV_MEM_SIZE (48U * 1024U)
    #define LV_MEM_ADR 0
    #define LV_MEM_AUTO_DEFRAG 1
#endif

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

/*=========================
   OPERATING SYSTEM
 *=========================*/
#define LV_USE_OS LV_OS_PTHREAD
#define LV_USE_USER_DATA 1

/*==================
   INPUT DEVICES
 *==================*/
#define LV_USE_INDEV_POINTER 1
#define LV_USE_INDEV_KEYPAD 1
#define LV_USE_INDEV_ENCODER 0
#define LV_USE_INDEV_BUTTON 0

/*================
   FEATURE USAGE
 *================*/
#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW 1
#define LV_USE_BLEND_MODES 1
#define LV_USE_OPA_SCALE 1
#define LV_USE_IMG_TRANSFORM 1

/*==================
   FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_38 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_42 1
#define LV_FONT_MONTSERRAT_44 1
#define LV_FONT_MONTSERRAT_46 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*=================
   WIDGET USAGE
 *=================*/
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_IMG 1
#define LV_USE_LIST 1
#define LV_USE_DROPDOWN 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_MSGBOX 1
#define LV_USE_TABVIEW 1
#define LV_USE_CHART 1
#define LV_USE_BAR 1

/*==================
   THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*=================
   LAYOUTS
 *=================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif /*LV_CONF_H*/