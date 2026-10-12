# 通用 AI Coding Skill 使用方式

项目无关的 Skill 位于 [constrained-ai-coding](../../skills/constrained-ai-coding/SKILL.md)。
它把需求转成任务契约、最小切片、验证闭环和可恢复检查点；项目自己的
`AGENTS.md`、构建脚本、验收命令和领域规范仍然优先。

## 新项目启动提示词

```text
使用 $constrained-ai-coding。
先阅读当前项目的 AGENTS.md、README、构建配置、测试入口和与本任务相关的代码。
把以下需求整理成 objective、non-goals、scope、constraints、acceptance、assumptions，
并判断这是 bug fix、feature、refactor、review、design/research 或 build/integration。
只在歧义会改变外部行为、数据安全、兼容性或任务范围时提问，否则记录假设并继续。
把任务拆成最小可验证切片，每次只完成一个切片；实际运行检查，记录命令、退出码、
失败分类和日志/工件路径。不要顺手扩展功能、重构无关代码或修改期望来掩盖失败。
完成后审查 diff，更新项目已有的任务/日志文档，报告已验证项、未验证项、风险和下一步。

项目需求：
<在这里粘贴需求>

项目约束：
<技术栈、版本、不可修改区域、性能/安全/权限要求>

验收方式：
<测试命令、运行场景、接口断言、工件或人工检查>
```

## 继续执行提示词

```text
使用 $constrained-ai-coding，读取最新任务契约和检查点。
只执行下一个未完成的切片，保持已验证输入和验收标准不变；先重现当前失败或确认前置状态，
再做最小修改并运行相关检查。完成后更新状态，保留首个失败证据，并给出下一步。
```

## 什么时候拆成项目专用 Skill

只有当多个项目任务共享稳定的领域知识、固定工具调用或确定性脚本时，才在这个通用 Skill
之上建立项目专用 Skill。项目专用内容应引用而不是复制通用流程，并明确其适用边界；
例如某个模拟器的 ISA profile、Linux 镜像布局、RTL 对拍协议不应污染通用 Skill。
