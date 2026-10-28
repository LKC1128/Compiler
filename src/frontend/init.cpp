#include "frontend/ir_builder.hpp"
#include <algorithm>

namespace sysy {
// A braced item initializes the largest subobject beginning at the current
// element, not an arbitrarily rounded-up row. Scalars use brace elision.
void IRBuilder::plan_initializer(const InitValAST* initializer, const std::vector<int>& dims,
                                 size_t dimension, size_t base, std::vector<InitEntry>& entries) {
    if (!initializer) return;
    size_t extent=1;
    for(size_t i=dimension;i<dims.size();++i) extent*=static_cast<size_t>(dims[i]);
    if (!initializer->is_list) {entries.push_back({base,0,initializer->expr.get()});return;}
    if (initializer->list.empty()) {entries.push_back({base,extent,nullptr});return;}
    if (extent==0) {
        if (dimension>=dims.size() || initializer->list.size()>static_cast<size_t>(dims[dimension]))
            report_error("Too many zero-sized array initializers",initializer->line,initializer->col);
        for(const auto& item:initializer->list) {
            if(!item->is_list) report_error("Initializer for zero-sized array",item->line,item->col);
            plan_initializer(item.get(),dims,dimension+1,base,entries);
        }
        return;
    }
    size_t cursor=0;
    for(const auto& item:initializer->list) {
        if(cursor>=extent) report_error("Too many array initializers",initializer->line,initializer->col);
        if(!item->is_list) {
            entries.push_back({base+cursor,0,item->expr.get()});++cursor;
        } else {
            size_t child_dimension=std::min(dimension+1,dims.size());
            size_t child_extent=1;
            for(size_t i=child_dimension;i<dims.size();++i) child_extent*=static_cast<size_t>(dims[i]);
            while(child_dimension<dims.size() && cursor%child_extent!=0) {
                child_extent/=static_cast<size_t>(dims[child_dimension]);++child_dimension;
            }
            plan_initializer(item.get(),dims,child_dimension,base+cursor,entries);
            cursor+=child_extent;
        }
    }
}
} // namespace sysy
