# Demo

`demo.p3d` 是 PC 播放器的最小示例：

- 16×16 地砖一层；
- 一个 16×16 玩家方块；
- 左右键移动；
- HUD 显示 “PocketLoom Demo”。

运行：

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d samples\demo\demo.p3d
~~~

发布包里从包根目录运行：

~~~powershell
.\bin\PocketLoom-Play.exe --p3d samples\demo\demo.p3d
~~~

重新生成：

~~~powershell
.\build\host\bin\make-demo.exe samples\demo\demo.p3d
~~~
