idf_component_register(SRCS "heart_and_SPO2_system.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver MAX30102 ppg_algorithm MLX90614 
                    )