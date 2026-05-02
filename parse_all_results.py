import os
import re
import json

def parse_stats(stats_path):
    if not os.path.exists(stats_path):
        return None
    with open(stats_path, 'r') as f:
        content = f.read()
        # gem5 stats file uses simTicks (case sensitive, no underscore usually)
        match = re.search(r'simTicks\s+(\d+)', content)
        if match:
            return int(match.group(1))
    return None

def main():
    root_dir = r'c:\cmu\comp_arch\18-742-Project\bitshift\18-742-Project'
    bench_dir = os.path.join(root_dir, 'benchmark_results')
    shift_dir = os.path.join(root_dir, 'shift_results')

    results = {
        'macroworkloads': {},
        'shifts': {}
    }

    # Parse macroworkloads
    if os.path.exists(bench_dir):
        for workload in os.listdir(bench_dir):
            if workload.startswith('02_gemm-plus-test'):
                continue
            
            workload_path = os.path.join(bench_dir, workload)
            if not os.path.isdir(workload_path):
                continue
                
            logs_dir = os.path.join(workload_path, 'run_logs')
            if not os.path.exists(logs_dir):
                continue
                
            results['macroworkloads'][workload] = {}
            
            for tm in ['timing_model_0', 'timing_model_1', 'timing_model_2']:
                tm_dir = os.path.join(logs_dir, tm)
                if not os.path.exists(tm_dir):
                    continue
                    
                results['macroworkloads'][workload][tm] = {'baseline': {}, 'pim': {}}
                
                for variant in ['baseline', 'pim']:
                    v_dir = os.path.join(tm_dir, variant)
                    if not os.path.exists(v_dir):
                        continue
                        
                    for config in os.listdir(v_dir):
                        stats_path = os.path.join(v_dir, config, 'stats.txt')
                        ticks = parse_stats(stats_path)
                        if ticks:
                            results['macroworkloads'][workload][tm][variant][config] = ticks

    # Parse shifts
    shift_logs = os.path.join(shift_dir, 'run_logs')
    if os.path.exists(shift_logs):
        for item in os.listdir(shift_logs):
            item_path = os.path.join(shift_logs, item)
            if not os.path.isdir(item_path):
                continue
                
            if item.startswith('timing_model_'):
                tm = item
                if tm not in results['shifts']: results['shifts'][tm] = {}
                for direction in os.listdir(item_path):
                    results['shifts'][tm][direction] = {'baseline': {}, 'pim': {}}
                    dir_path = os.path.join(item_path, direction)
                    for variant in ['baseline', 'pim']:
                        v_dir = os.path.join(dir_path, variant)
                        if not os.path.exists(v_dir): continue
                        for config in os.listdir(v_dir):
                            stats_path = os.path.join(v_dir, config, 'stats.txt')
                            ticks = parse_stats(stats_path)
                            if ticks:
                                results['shifts'][tm][direction][variant][config] = ticks
            else:
                # No timing model subdirectory
                tm = 'timing_model_0' # Default
                if tm not in results['shifts']: results['shifts'][tm] = {}
                direction = item
                results['shifts'][tm][direction] = {'baseline': {}, 'pim': {}}
                for variant in ['baseline', 'pim']:
                    v_dir = os.path.join(item_path, variant)
                    if not os.path.exists(v_dir): continue
                    for config in os.listdir(v_dir):
                        stats_path = os.path.join(v_dir, config, 'stats.txt')
                        ticks = parse_stats(stats_path)
                        if ticks:
                            results['shifts'][tm][direction][variant][config] = ticks

    with open('parsed_results.json', 'w') as f:
        json.dump(results, f, indent=2)

if __name__ == '__main__':
    main()
