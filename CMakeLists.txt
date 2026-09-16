cmake_minimum_required(VERSION 3.16)
set(CONFIG_PARTITION_TABLE_CUSTOM "y")
set(CONFIG_PARTITION_TABLE_FILENAME "partitions.csv")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32-S3-Frontend-IDF)

# Genera l'immagine SPIFFS e la flasha automaticamente
# spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)

# Genera l'immagine SPIFFS ma NON la flasha automaticamente
spiffs_create_partition_image(spiffs data)
