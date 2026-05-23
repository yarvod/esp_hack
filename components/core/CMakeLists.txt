idf_component_register(
    SRCS
        "src/app_manager.c"
        "src/animation.c"
        "src/context.c"
        "src/event_bus.c"
        "src/input_dispatcher.c"
        "src/navigation.c"
        "src/screen_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES ui esp_timer
)
