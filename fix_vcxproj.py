import re
with open('BlazeCRT/Win/BlazeCRT.vcxproj', 'r', encoding='utf-8') as f:
    content = f.read()

# Remove OpenCL compile entry
pattern = r'<ClCompile Include="\.\.[/\\]BlazeCRT_OpenCL\.cpp"[^/]*/>'
content = re.sub(pattern, '', content)

# Remove OpenMP
content = content.replace('<OpenMPSupport>true</OpenMPSupport>', '')

with open('BlazeCRT/Win/BlazeCRT.vcxproj', 'w', encoding='utf-8') as f:
    f.write(content)

print('Done')
