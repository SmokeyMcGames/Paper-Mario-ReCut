#include "librecomp/overlays.hpp"

#include "../../n64recomp-n64recomp-https-github-com-n64recomp/paper_mario_recomp_out/recomp_overlays.inl"

namespace paper_mario {
    void register_overlays() {
        recomp::overlays::overlay_section_table_data_t sections{
            .code_sections = section_table,
            .num_code_sections = ARRLEN(section_table),
            .total_num_sections = num_sections,
        };

        recomp::overlays::overlays_by_index_t overlays{
            .table = overlay_sections_by_index,
            .len = ARRLEN(overlay_sections_by_index),
        };

        recomp::overlays::register_overlays(sections, overlays);
    }
}
