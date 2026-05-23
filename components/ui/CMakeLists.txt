idf_component_register(
    SRCS
        "src/ui.c"
        "src/ui_carousel.c"
        "src/ui_font5x7.c"
        "src/ui_menu.c"
        "src/ui_status_bar.c"
        "src/ui_widgets.c"
    INCLUDE_DIRS "include"
    REQUIRES drivers esp_timer
)
