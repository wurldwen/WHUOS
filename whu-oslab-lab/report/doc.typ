
#import "lib.typ": *

// 可配置信息

#let cover_header = "武汉大学计算机学院"
#let report_title = "本科生实验报告"
#let title = "os内核实验"
#let course = "操作系统实践A"
#let major = "计算机科学与技术"
#let teacher1_name = "蔡朝晖"
#let teacher1_title = "教授"
#let teacher2_name = none
#let teacher2_title = none
#let student_id = "2023302111199"
#let student_name = "黄文婷"
#let year = "2025"
#let month = "12"
#let maketitle = true
#let makeabstract = false
#let makeoutline = true
#let outline-depth = 2
#let first-line-indent = auto
#let font = none // 使用默认 font

#let abstract = [
]

#let teacher1 = (teacher1_name, teacher1_title)
#let teacher2 = if teacher2_name == none or teacher2_name == "" {
  none
} else {
  (teacher2_name, teacher2_title)
}

#let keywords = ()

// 自定义字体配置（使用SimSun宋体和SimHei黑体）
#let custom_font = (
  main: "Liberation Serif", // Times New Roman的替代字体
  mono: "IBM Plex Mono",
  cjk: "SimSun", // 宋体
  cjk-bold: "SimHei", // 黑体
  emph-cjk: "AR PL UKai", // 文鼎楷体
  math: "New Computer Modern Math",
  math-cjk: "Noto Serif SC",
)

#show: ori.with(
  cover_header: cover_header,
  report_title: report_title,
  title: title,
  course: course,
  major: major,
  teacher1: teacher1,
  teacher2: teacher2,
  student_id: student_id,
  student_name: student_name,
  year: year,
  month: month,
  maketitle: maketitle,
  makeabstract: makeabstract,
  abstract: [
    #abstract
  ],
  keywords: keywords,
  makeoutline: makeoutline,
  outline-depth: outline-depth,
  first-line-indent: (amount: 2em, all: true), // 首行缩进2字符
  font: custom_font, // 使用自定义字体
  size: 12pt, // 小四号对应12pt
)
#import "@preview/cmarker:0.1.7"

#cmarker.render(
  read("content.md"),
  math: mitex,
  scope: (image: (source, alt: none, format: auto) => image(source, alt: alt, format: format))  
)