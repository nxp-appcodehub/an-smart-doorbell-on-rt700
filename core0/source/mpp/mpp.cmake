#list mpp app task and face recognition models specific source files

mcux_add_source(
    BASE_PATH ${SdkRootDirPath}
    SOURCES   middleware/eiq/mpp/hal/hal_${board}.c
)

# model and post-processing
mcux_add_include(
    BASE_PATH ${SdkRootDirPath}/middleware/eiq/mpp
    INCLUDES    models
                models/mobilefacenet
                models/antispoofing
                models/scrfd_kps_500m_full_integer_quant
                common
                hal/include
)

mcux_add_source(
    BASE_PATH ${SdkRootDirPath}/middleware/eiq/mpp
    SOURCES models/get_top_n.cpp
            common/draw_text.c
            models/utils.cpp
            models/get_top_n.h
            models/utils.h
            models/database_utils.h
            models/database_utils.c
            models/mobilefacenet/mobilefacenet_output_postproc_quantized.cpp
            models/mobilefacenet/*.h
            models/antispoofing/antispoofing_output_postproc_quantized.cpp
            models/face_box_utils.c
            models/antispoofing/*.h
            models/scrfd_kps_500m_full_integer_quant/scrfd_kps_output_postproc.c
            models/scrfd_kps_500m_full_integer_quant/*.h
)
