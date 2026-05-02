import json
import matplotlib.pyplot as plt
import numpy as np
import os
import re
from scipy.stats import gmean

def get_size_rank(config):
    match = re.search(r'_s(\d+)', config)
    if match:
        return int(match.group(1))
    return -1

def safe_gmean(arr):
    arr = [x for x in arr if x > 0]
    if not arr: return 0
    return gmean(arr)

def main():
    with open('parsed_results.json', 'r') as f:
        data = json.load(f)

    output_dir = 'figures'
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # 1. Speedup results between baseline and pim on timing model 0 (EVERY SIZE)
    macro = data['macroworkloads']
    
    # Identify ALL available size ranks across all workloads
    all_ranks = set()
    for wl_name in macro:
        tm0 = macro[wl_name].get('timing_model_0', {})
        for variant in ['baseline', 'pim']:
            for config in tm0.get(variant, {}):
                rank = get_size_rank(config)
                if rank != -1: all_ranks.add(rank)
    
    sorted_ranks = sorted(list(all_ranks))
    print(f"Plotting all size ranks: {sorted_ranks}")

    wl_names = []
    # Data structure: rank -> list of average speedups per workload
    rank_to_data = {r: [] for r in sorted_ranks}
    
    sorted_wl = sorted(macro.keys())
    for wl_name in sorted_wl:
        tm0 = macro[wl_name].get('timing_model_0')
        if not tm0: continue
        
        baseline = tm0.get('baseline', {})
        pim = tm0.get('pim', {})
        
        # Group by size rank for THIS workload
        local_rank_to_speedups = {}
        for config in baseline:
            if config in pim:
                rank = get_size_rank(config)
                if rank not in local_rank_to_speedups: local_rank_to_speedups[rank] = []
                local_rank_to_speedups[rank].append(baseline[config] / pim[config])
        
        if local_rank_to_speedups:
            wl_names.append(wl_name.replace('0', '').replace('_', ' ').strip())
            for rank in sorted_ranks:
                if rank in local_rank_to_speedups:
                    rank_to_data[rank].append(np.mean(local_rank_to_speedups[rank]))
                else:
                    rank_to_data[rank].append(0) # Padding for alignment

    # Add GeoMean
    wl_names.append('GEOMEAN')
    for rank in sorted_ranks:
        g = safe_gmean(rank_to_data[rank])
        rank_to_data[rank].append(g)

    # Plotting
    plt.figure(figsize=(20, 10))
    x = np.arange(len(wl_names))
    total_width = 0.85
    width = total_width / len(sorted_ranks)
    
    # Color map for sizes
    cmap = plt.get_cmap('coolwarm')
    colors = [cmap(i / max(1, len(sorted_ranks)-1)) for i in range(len(sorted_ranks))]

    for i, rank in enumerate(sorted_ranks):
        offset = (i - (len(sorted_ranks)-1)/2) * width
        plt.bar(x + offset, rank_to_data[rank], width, label=f'Size s{rank}', color=colors[i], edgecolor='black', linewidth=0.5, alpha=0.9)

    plt.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5)
    plt.ylabel('Speedup (Baseline / PIM)')
    plt.yscale('log')
    plt.title('PIM Speedup by Workload Size (All Tested Sizes)')
    plt.xticks(x, wl_names, rotation=35, ha='right')
    plt.gca().get_xticklabels()[-1].set_fontweight('bold')
    plt.legend(title='Problem Size (sX)', ncol=len(sorted_ranks)//2 + 1)
    plt.grid(axis='y', linestyle=':', alpha=0.6)
    
    # Add labels only for the GEOMEAN bars to keep it clean
    for i, rank in enumerate(sorted_ranks):
        val = rank_to_data[rank][-1] # Geomean is the last element
        if val > 0:
            offset = (i - (len(sorted_ranks)-1)/2) * width
            plt.text(len(wl_names)-1 + offset, val * 1.05, f'{val:.1f}x', ha='center', va='bottom', fontsize=8, rotation=90)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, '01_speedup_by_size_all.png'), dpi=200)
    plt.close()

    # 2. Timing Model Comparison (STRICTLY Largest available rank)
    # Keeping this one as is but naming it explicitly
    plt.figure(figsize=(12, 6))
    tm1_rel = []
    tm2_rel = []
    tm_wl_names = []
    
    for wl_name in sorted_wl:
        tm0_pim = macro[wl_name].get('timing_model_0', {}).get('pim', {})
        tm1_pim = macro[wl_name].get('timing_model_1', {}).get('pim', {})
        tm2_pim = macro[wl_name].get('timing_model_2', {}).get('pim', {})
        if not tm0_pim: continue
        
        available_ranks = sorted([get_size_rank(c) for c in tm0_pim])
        if not available_ranks: continue
        max_rank = available_ranks[-1]
        
        tm1_speeds = []
        tm2_speeds = []
        for config in tm0_pim:
            if get_size_rank(config) == max_rank:
                if config in tm1_pim: tm1_speeds.append(tm0_pim[config] / tm1_pim[config])
                if config in tm2_pim: tm2_speeds.append(tm0_pim[config] / tm2_pim[config])
        
        if tm1_speeds or tm2_speeds:
            tm_wl_names.append(wl_name.replace('0', '').replace('_', ' ').strip())
            tm1_rel.append(np.mean(tm1_speeds) if tm1_speeds else 0)
            tm2_rel.append(np.mean(tm2_speeds) if tm2_speeds else 0)

    if tm_wl_names:
        tm_wl_names.append('GEOMEAN')
        tm1_rel.append(safe_gmean(tm1_rel))
        tm2_rel.append(safe_gmean(tm2_rel))
        
        x = np.arange(len(tm_wl_names))
        width_tm = 0.35
        plt.bar(x - width_tm/2, tm1_rel, width_tm, label='TM0 / TM1 (Best Case)', color='#2a9d8f')
        plt.bar(x + width_tm/2, tm2_rel, width_tm, label='TM0 / TM2 (Conservative)', color='#f4a261')
        
        plt.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5)
        plt.ylabel('Relative Performance (vs TM0)')
        plt.title('Performance Scaling (Measured on Max Available Size)')
        plt.xticks(x, tm_wl_names, rotation=35, ha='right')
        plt.gca().get_xticklabels()[-1].set_fontweight('bold')
        plt.legend()
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, '02_timing_model_scaling_large.png'), dpi=200)
    plt.close()

    print(f"Figures saved in {output_dir}")

if __name__ == '__main__':
    main()
