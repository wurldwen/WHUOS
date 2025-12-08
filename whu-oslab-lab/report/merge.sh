#!/bin/bash
output="content.md"
> "$output"  # 清空或创建文件
for i in {1..10}; do
    if [ -f "${i}.md" ]; then
        cat "${i}.md" >> "$output"
        echo "" >> "$output"  # 添加空行分隔章节
    fi
done
echo "合并完成，输出到 $output"