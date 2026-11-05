# 示例

## Bee Swatter / 打蜜蜂

工程目录：`samples/bee`

- 在编辑器里打开 `samples/bee/project.json`；
- F5 试玩；
- 左右键移动，X 发射；
- 蜜蜂从右往左飞，子弹命中后消失；
- HUD 显示操作提示。

内容全部是编辑器数据：

- 场景：`scenes/garden.json`；
- 对象：`objects/swatter.json` / `objects/bee.json` / `objects/sting.json`；
- 动画：`animations/*.json`；
- 规则：玩家移动 / 发射 / 蜜蜂飞行都在对象 JSON 的 rules 里；
- 界面：`ui/hud.json`。

`bee.p3d` 是用编辑器命令行重新导出的：

~~~powershell
.\build\host\bin\ploom.exe --open samples\bee\project.json --export-p3d samples\bee\bee.p3d
~~~

直接运行：

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d samples\bee\bee.p3d
~~~

## Demo

`samples/demo/demo.p3d` 是最小渲染示例：

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d samples\demo\demo.p3d
~~~
