import os

def refactor_struct(filepath, struct_name, type_name):
    with open(filepath, 'r') as f:
        lines = f.readlines()
        
    start_idx = -1
    end_idx = -1
    
    # Find the start of the struct definition
    for i, line in enumerate(lines):
        if f"{type_name} {struct_name} =" in line or f"{type_name} {struct_name}=" in line:
            start_idx = i
            break
            
    if start_idx == -1: return False
    
    # Find the end of the struct definition
    for i in range(start_idx, len(lines)):
        if "};" in lines[i]:
            end_idx = i
            break
            
    if end_idx == -1: return False
    
    # Extract assignments
    assignments = []
    for i in range(start_idx + 2, end_idx):
        line = lines[i].strip()
        if not line or line.startswith('//') or line.startswith('/*'): continue
        
        # Remove trailing comments
        code_part = line.split('/*')[0].split('//')[0].strip()
        if not code_part: continue
        
        if code_part.startswith('.'):
            parts = code_part.split('=', 1)
            if len(parts) == 2:
                key = parts[0][1:].strip() # remove leading '.'
                val = parts[1].strip().rstrip(',')
                # Handle nested struct manually if present
                if key == "RtiaCalValue":
                    pass # ignore it, not needed
                elif key == "RtiaCalValue.Magnitude":
                    pass # ignore it
                else:
                    assignments.append(f"  {struct_name}.{key} = {val};")
                    
    # Generate new code block
    new_decl = f"{type_name} {struct_name};\n\n"
    init_func_name = f"_Init_{struct_name}"
    init_func = f"void {init_func_name}() {{\n" + "\n".join(assignments) + "\n}\n"
    
    # Replace old block
    new_lines = lines[:start_idx] + [new_decl, init_func] + lines[end_idx+1:]
    
    # Inject call into GetCfg function
    get_cfg_line_idx = -1
    for i, line in enumerate(new_lines):
        if f"Err App{struct_name.replace('App', '').replace('Cfg', '')}GetCfg(void *pCfg)" in line:
            get_cfg_line_idx = i
            break
            
    if get_cfg_line_idx != -1:
        # Find opening brace
        for i in range(get_cfg_line_idx, len(new_lines)):
            if "{" in new_lines[i]:
                injection = f"\n  static bool initialized = false;\n  if(!initialized) {{ {init_func_name}(); initialized = true; }}\n"
                new_lines.insert(i + 1, injection)
                break
                
    with open(filepath, 'w') as f:
        f.writelines(new_lines)
        
    print(f"Successfully refactored {filepath}")
    return True

refactor_struct('Software/AD5941_CA/ChronoAmperometric.cpp', 'AppCHRONOAMPCfg', 'AppCHRONOAMPCfg_Type')
refactor_struct('Software/AD5941_CA/rampTest.cpp', 'AppRAMPCfg', 'AppRAMPCfg_Type')
