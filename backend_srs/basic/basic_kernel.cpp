#include "basic_kernel.h"
#include "utility.h"
#include "c_codegen.h"
#include "basic/basic_processes.h"
#include <iostream>
#include <algorithm>
#include <functional>
#include <unordered_map>
using namespace std;

void set_marker_locs_to(vector<bool> & locs,marker_g & marks,bool value){
    for(mark_ty mark : marks){
        locs[mark] = value;
    }
}

basic_kernel::basic_kernel(string inname, GraphBuilder & graph,
             marker_g new_in_nodes,
             marker_g final_out_nodes,
             marker_g inter_in_nodes,
             marker_g inter_out_nodes,
             marker_g const_nodes){
    new_ins = new_in_nodes;
    fin_outs = final_out_nodes;
    inter_ins = inter_in_nodes;
    inter_outs = inter_out_nodes;
    name = inname;
    
    marker_g sorted_nodes;
    sort_needed_nodes(sorted_nodes,graph,const_nodes);
    build_compnode_graph(sorted_nodes,graph);
}
void basic_kernel::sort_needed_nodes(marker_g & out_sorted_nodes,GraphBuilder & graph,marker_g & const_nodes){
    vector<bool> is_important(graph.elements(),false);

    vector<bool> do_not_calc(graph.elements(),false);

    set_marker_locs_to(is_important,inter_outs,true);
    set_marker_locs_to(is_important,fin_outs,true);

    set_marker_locs_to(do_not_calc,new_ins,true);
    set_marker_locs_to(do_not_calc,inter_ins,true);
    set_marker_locs_to(do_not_calc,const_nodes,true);

    auto set_if_should_calc = [&](mark_ty mark){
        if(!do_not_calc[mark]){
            is_important[mark] = true;
        }
    };

    for(int64_t i = is_important.size()-1; i >= 0; i--){
        if(is_important[i] && !do_not_calc[i]){
            for(mark_ty in_mark  : graph.computes[i].inputs){
                set_if_should_calc(in_mark);
            }
        }
    }
    for(mark_ty mark = 0; mark < graph.elements(); mark++){
        if(is_important[mark]){
            out_sorted_nodes.push_back(mark);
        }
    }
}
unordered_map<mark_ty,size_t> ty_mark_map(marker_g & sorted_nodes, GraphBuilder & graph,function<bool(start::obj)> is_of_type){
    unordered_map<mark_ty,size_t> res;
    size_t idx = 0;
    for(mark_ty mark : sorted_nodes){
        if(is_of_type(graph.computes[mark])){
            res[mark] = idx;
            idx++;
        }
    }
    return res;
}
void add_marks_to_buf_idx(unordered_map<mark_ty,size_t> & bufidxs,size_t maxidx,const marker_g & all_marks){
    for(mark_ty mark : all_marks){
        bufidxs[mark] = maxidx;
        maxidx++;
    }
}

unordered_map<mark_ty,size_t> mark_to_buf_idx(const marker_g & all_marks){
    unordered_map<mark_ty,size_t> res;
    add_marks_to_buf_idx(res,0,all_marks);
    return res;
}


void basic_kernel::build_compnode_graph(marker_g & sorted_nodes, GraphBuilder & graph, default_process_generator & proc_gen){
    memory.clear();
    nodes.clear();
    unordered_map<mark_ty,size_t> mem_map = ty_mark_map(sorted_nodes,graph,[](start::obj){return true;});
    unordered_map<mark_ty,size_t> input_map = ty_mark_map(sorted_nodes,graph,[](start::obj node){return node.ty == start::INPUT;});
    auto new_mem = [&](mark_ty mark){
        //returns
        size_t mem_id = memory.size();
        mem_map[mark] = mem_id;
        memory.push_back(abst_memory{mem_id,{}});
        return mem_id;
    };
    auto register_input_for = [&](mark_ty input,size_t outputid){
        memory[mem_map[input]].compdestids.push_back(outputid);
    };

    for(mark_ty mark : sorted_nodes){
        using namespace start;
        obj node = graph.computes[mark];
        size_t memid = new_mem(mark);
        memory.push_back(abst_memory{memid,{}});
        for(mark_ty m : node.inputs){
            register_input_for(m,memid);
        }
        switch(node.ty){
        case CONST:
        {
            process * nodeproc = proc_gen.store_proc(procptr(new const_float_val{node.myunion.const_d.val}));
            nodes.push_back(compute_node{{},nodeproc,{memid}});
        }
            break;
        case BIN:
        {
            process * nodeproc = proc_gen.store_proc(procptr(new bin_op_proc{node.myunion.bin_d.op}));
            nodes.push_back(compute_node{{},nodeproc,{memid}});
        }
            break;
        case UN:
        {
            process * nodeproc = proc_gen.store_proc(procptr(new uni_op_proc{node.myunion.un_d.op}));
            nodes.push_back(compute_node{{},nodeproc,{memid}});
        }
            break;
        case INPUT:
        {
            process * nodeproc = proc_gen.store_proc(procptr(new const_float_val{node.myunion.in_d.mark}));
            nodes.push_back(compute_node{{},nodeproc,{memid}});
        }
            break;
            nodes.push_back(proc_gen.store_proc(procptr(new const_float_val{node.myunion.in_d.mark}))); 
            break;
        case STORED:
            nodes.push_back(proc_gen.store_proc(procptr(new const_float_val{node.myunion.stor_d.initval})));
            break;
        }
    }
}

std::string basic_kernel::to_string(GraphBuilder & graph){
    string body_string;// = make_string(depth_sorted,graph);

    for(mark_ty mark : sorted_all_nodes){
        oper mop = graph.node_op[mark];
        if(op::is_bin(mop)){
            body_string += assign_str(entry(mark),bin_str(entry(graph.first[mark]),entry(graph.second[mark]),mop));
        }
        else if(op::is_uni(mop)){
            body_string += assign_str(entry(mark),uni_str(entry(graph.first[mark]),mop));
        }
        else if(op::is_const(mop)){
            //ExitError("runaway constnode");
        }
        else{
            ExitError("hit bad node type");
        }
    }

    for(size_t i = 0; i < inter_ins.size(); i++){
        body_string += assign_str(entry(inter_ins[i]),entry(inter_outs[i]));
    }

    return " void " + fun_str(name,{}) + "{" + body_string + "}";
}
