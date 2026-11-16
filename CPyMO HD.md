# CPyMO HD

> **实现状态（main 分支）**
>
> 本文是 CPyMO HD 的分级功能规划。`main` 已落地 Feature Level 1 的 Lua 部分：`#lua` 命令、
> Actor 抽象、`cpymo` 下的各 API 包（`render` / `asset` / `ui` / `flags` / `input` / `script` /
> `save`），以及 `gameconfig.txt` 里的 `cpymohd1` / `cpymohd2` / `cpymohd3` 平台值。
> 开启方式见 `README.md` 的 libpymo 一节。
>
> `main` 不做该级别里的删减项：不移除 `s60v3` / `s60v5` 平台支持，也不跳过启动时的
> logo1 / logo2，Feature Level 1 与 Level 0 一样播放启动 logo。
>
> 尚未开始：Feature Level 2 的图形后端增强（旋转、缩放、加法混合）与 Feature Level 3 的
> OpenGL ES 2.0 完整访问。

CPyMO HD现在使用的Lua版本是`v5.4.6`。

*CPyMO HD*是CPyMO功能增强计划的开发代号，在`hd`分支上开发。
CPyMO HD将引擎扩展功能划分为四个功能级别，每个级别都基于上个级别进行改动：

- Feature Level 0
  * 这个级别和PyMO完全一致。
- Feature Level 1
  * 该级别的backend部分与Feature Level 0保持一致，不需要增强硬件功能。
  * 该级别可能会占用更多的内存，除此之外，所有Feature Level 0可以运行的平台都应可以运行Feature Level 1.
- Feature Level 2
  * 这个级别在backend上增强图形功能：
    + 旋转
    + 缩放
    + 加法混合模式
- Feature Level 3
  * 这个级别在backend上增强图形功能以使得CPyMO可以完整访问Open GL ES 2.0的所有功能。

每个CPyMO的编译版本都可以支持不同的功能级别，而游戏可以在`gameconfig.txt`的`platform`字段中指定以下值：

- `cpymohd1` - 最低需要使用CPyMO HD Feature Level 1来执行
- `cpymohd2` - 最低需要使用CPyMO HD Feature Level 2来执行
- `cpymohd3` - 最低需要使用CPyMO HD Feature Level 3来执行

除了Feature Level 0之外，每个Feature Level都可以直接访问更低Feature Level的功能，并且需要插入Feature Level判断代码来可选地使用更高Feature Level的功能。


# Feature Level 1

* 支持Lua语言，在pymo脚本中新增`#lua`命令，此行后面的部分会被直接当作lua代码执行
* `#lua` 块整体运行在一个Lua协程里，可以用 `cpymo.wait` / `cpymo.yield` 跨帧挂起；同一时刻来了新的 `#lua` 块时，尚未跑完的旧块会被丢弃

## 在 YukimiScript 中使用

`libpymo.ykm` 已声明 `- extern lua code`（`@__type code string`），把 Lua 送进引擎就是一行：

```
@lua "cpymo.a11y.speak('第一章')"
```

三条限制来自编译器，不是 CPyMO 的：

* 代码必须是双引号字符串。PyMO 代码生成器只认识 `true`、`false`、`null`、`a`、`BG_*`、`cm0`~`cm6` 这些裸符号，其余裸符号会让编译直接失败（`Unsupported PyMO keyword`）。
* 建议一条 `@lua` 只写一行，多条语句用 `;` 连接。Lua 后端（CPyMO HD 实际使用的产物）会把 `\n`、引号重新转义，换行不会出错；但 PyMO 文本后端是原样写入，换行会把那一行撕开。单行写法两边都安全。
* 命令名本身没有白名单，但每个命令都必须在 `libpymo.ykm` 里 `extern` 声明过，否则编译报 `ExternCommandDefinationNotFoundException`；参数个数与类型也在编译期检查，参数顺序按声明顺序重排，具名参数可以乱序。

`#lua` 之后整行余下的部分原样交给 Lua，引擎不按逗号切分，所以代码里的逗号和括号都不用转义。Level 0 构建不认识 `#lua`，只会打印未知命令警告后继续执行。

已有的 `.pymo` 老脚本不必手改：`pymo2ykm.ps1` 会把 `#lua ...` 这行原样转成 `@lua "..."`（`6be41bb` 起）。它不做 Lua 语法校验，写错要到运行时才暴露。

## Actor抽象


Actor是一种在游戏中可以被作为游戏物体执行的对象，它具有以下结构：

```lua

{
    -- 无返回值，用于在需要绘制该对象时进行绘制操作
    -- 不保证每帧都会调用
    -- 该函数不应该修改self的内容
    draw = function(self)
    end,

    -- 无返回值，在当前对象及所有子对象绘制结束之后调用
    late_draw = function(self)
    end,

    -- 无返回值，用于在需要更新该对象时进行更新操作
    -- 保证每帧调用一次
    -- 距离上一帧的时间将会被作为delta_time: float参数传入
    update = function(self, delta_time)
    end,

    -- 无返回值，用于在当前对象及所有子对象更新结束之后调用
    -- 保证每帧调用一次
    -- 距离上一帧的时间将会被作为delta_time: float参数传入
    late_update = function(self, delta_time)
    end,

    -- 在该对象即将被销毁时调用
    -- 需要在这里销毁所有的非GC对象
    free = function(self)
    end,

    -- 该对象及子对象已经被free了之后调用
    -- 用于销毁具有特殊依赖关系的非GC对象
    late_free = function(self)
    end,

    -- 该对象内将会被用于递归地存储子actor列表
    children = {},

    -- 仅对于main actor，在存档时调用，返回一个可序列化的值用于保存Lua脚本中需要存档的数据
    save = function()
        return serializable
    end

    -- 仅对于main actor，在读档时调用，需要在这里重置Lua脚本状态并读入存档
    load = function(serializable)
    end

    -- 所有的对象均可为nil，CPyMO将会跳过这些处理
    -- 除此之外，还可以放入其他对象用于保存本actor的状态信息
}

```

CPyMO将会从全局的`main`表作为main actor执行，在进入UI状态时将不会执行`main` actor。
CPyMO默认将会把`main`设置为一个空表。


## 工具类型

工具类型是一种Lua表，用于在Lua端和C端交换数据。

### rect

具有以下字段：

* `x: number`
* `y: number`
* `w: number`
* `h: number`

### color

具有以下字段，所有值范围均在0~255之间：

* `r: number`
* `g: number`
* `b: number`
* `a: number | nil`


## Lua API

所有的CPyMO Lua API都存储在包`cpymo`中。

其中可序列化类型`serializable`可以为`number`、`nil`、`string`、`bool`以及由这些类型构成的可嵌套`table`。

### `cpymo`

* `gamedir: string` - 表示游戏所在文件夹
* `feature_level: int` - 当前运行在哪个Feature Level的引擎上
  - 可能会大于`gameconfig.txt`中的值，这种情况下可以访问更高级别的功能
* `screen_width: int` - 屏幕宽度，用于确定绘图和鼠标的坐标系，读取自`gameconfig.txt`的`imagesize`字段
* `screen_height: int` - 屏幕高度，用于确定绘图和鼠标的坐标系，读取自`gameconfig.txt`的`imagesize`字段
* `readonly(table) : userdata` - 创建表的只读句柄，可以通过该句柄读取表的内容，但不能写入
* `is_skipping() : bool` - 检查是否正在跳过
* `extract_text(string)` - 导出游戏文本以供游戏在CUI模式下运行或提供给视障人员，需要多次调用后使用`extract_text_submit`来进行一次导出
* `extract_text_submit()` - 提交一次`extract_text`导出的文本
  - 只负责朗读通道，不会自动朗读`cpymo.render`绘制的文本，详见`cpymo.a11y`
* `serialize(serializable) : string` - 将lua值序列化为lua表达式字符串
* `deserialize(string) : serializable` - 将序列化后的lua表达式字符串反序列化为lua值
* `exit()` - 退出游戏
* `timer(delay: number, callback: function, ...) : int` - `delay` 秒后调用一次 `callback`，返回可供 `timer_cancel` 使用的id，其余参数原样传给 `callback`
* `every(interval: number, callback: function, ...) : int` - 每 `interval` 秒调用一次 `callback`，返回取消用id
* `timer_cancel(id: int)` - 取消一个由 `timer` 或 `every` 创建的定时器
* `wait(seconds: number)` - 挂起当前 `#lua` 块 `seconds` 秒；只能写在 `#lua` 块里，写在定时器回调里会报错
* `yield()` - 让出到下一帧，只能写在 `#lua` 块里

时序以游戏时间累加，菜单暂停时不走。定时器、挂起位置与常驻显示项都不写入存档，读档后需自行重建。
为保证可移植性，`io.read`、`os.execute`、`io.popen`、`debug.sethook` 与不带参数的 `io.lines` 已被替换为报错，指引改用 `cpymo.wait` / `cpymo.yield`。

### `cpymo.render`

供Lua绘制的屏幕空间的左上角坐标为`(0, 0)`，屏幕的长度和宽度由`gameconfig.txt`中`imagesize`字段定义。

这个包用于存储与渲染有关的功能，以下为此包内容：

* `request_redraw()` - 请求绘制新一帧
* `fill_rect(dst: rect, color, semantic)` - 在下一帧绘制一个实心矩形
* `create_text(text: string, fontsize: number) : cpymo_render_text` - 创建一个可供渲染的文本图像缓存

#### `cpymo.render.semantic`

这个包内包含了一些绘图语义：

* `bg` - 背景
* `chara` - 立绘
* `ui_bg` - UI层背景
* `ui_element_bg` - UI元素的背景
* `ui_element` - UI元素
* `text_say` - 对话文本
* `text_say_textbox` - 对话文本框
* `text_text` - text命令生成的文本
* `titledate_bg` - title_dsp命令与date命令生成的背景
* `titledate_text` - title_dsp命令与date命令生成的文本
* `sel_img` - 选项
* `effect` - 后处理特效

#### 类`cpymo_render_image`

该类表示一个可以渲染到画面上的图像。
你可以使用`<close>`在声明中标记该值。

这个类包含以下成员：

* `get_fontsize() : number` - 获取字体大小（根据屏幕缩放过）
* `get_fontsize_raw() : number` - 获取字体大小（原始值）
* `get_size(self) : number, number` - 获取大小
* `draw(self, dst: rect, src: rect | nil, alpha: number, semantic)` - 绘制此图像到下一帧，其中alpha范围在0~255之间
* `free(self)` - 手动销毁该对象

#### 类`cpymo_render_text`

该类表示一个可以渲染到画面上的文本。
你可以使用`<close>`在声明中标记该值。

这个类包含以下成员：

* `get_size(self) : number, number` - 获得大致宽度和fontsize，这些值仅供估算大小，具体值由字体系统的实现决定
* `draw(self, x: number, y_baseline: number, color, semantic)` - 使用给定颜色将此文本绘制到下一帧
* `free(self)` - 手动销毁该对象

#### 类`cpymo_render_masktrans`

该类表示一个过场遮罩。
并不是所有的平台都支持创建此对象，**所有返回该对象的函数都有可能返回关于平台不支持的异常**。
你可以使用`<close>`在声明中标记该值。

这个类包含以下成员：

* `draw(self, lerp: number, inverse: bool)`
  - 将该对象绘制到全屏幕上
  - 其中`lerp`处在0~1之间，表示从全可见到全不可见的中间插值状态
  - 如果`inverse`为`true`则会翻转每个像素
* `free(self)` - 手动销毁该对象

### `cpymo.asset`

* `load_bg(bg_name: string) : cpymo_render_image`
  - 通过不带扩展名和路径的名称加载bg图像
* `load_chara(chara_name: string) : cpymo_render_image`
  - 通过不带扩展名和路径的名称加载chara图像
* `load_system_image(image_name: string) : cpymo_render_image`
  - 通过不带扩展名和路径的名称加载system图像
* `load_system_masktrans(masktrans_name: string) : cpymo_render_masktrans`
  - 通过不带扩展名和路径的名称加载过场遮罩
* `load_image(path: string) : cpymo_render_image`
  - 从指定路径加载图片，需要扩展名
  - 请在路径开头处增加`cpymo.gamedir`确保访问正确的目录
* `open_package(path: string) : cpymo_asset_package`
  - 打开指定路径处的数据包
  - 请在路径开头处增加`cpymo.gamedir`确保访问正确的目录

#### 类`cpymo_asset_package`

该类表示一个被打开的数据包。
你可以使用`<close>`在声明中标记该值。

该类包含以下成员：

* `load_string(self, filename: string) : string`
  - 从数据包中加载字符串
  - 其中文件名不带扩展名
* `load_image(self, filename: string) : cpymo_render_image`
  - 从数据包中加载图片
  - 其中文件名不带扩展名
* `free(self)` - 手动销毁该对象

### `cpymo.ui`

* `enter(ui: actor)` - 进入一层UI
* `exit()` - 退出当前UI
* `msgbox(msg: string)` - 弹出一个消息框
* `okcancelbox(msg: string, callback: bool -> void)`
  - 弹出“确定/取消”框，当关闭此框时调用`callback`
  - 若选择了“确定”，则`callback`会被传入`true`，其他情况下传入`false`

* `open_backlog()` - 打开对话历史记录
* `open_config()` - 打开设置界面
* `open_musicbox()` - 打开音乐鉴赏界面
* `open_rmenu()` - 打开右键菜单
* `open_save_ui()` - 打开存档界面
* `open_load_ui()` - 打开读档界面
* `play_movie(name: string)` - 播放video文件夹中的视频
* `open_album(list_name: string, bg_image: string)`
  - 打开album界面
  - `list_name`为CG列表的文件名
  - `bg_image`为system目录下背景图的文件名
  - 该命令在有提前渲染好的缓存图片的情况下会调用缓存图片，否则会创建缓存图片
* `open_load_yesnobox(save_id: int)` - 打开读档的“是否”对话框
* `hud(id: any, text: string, options: table?)` - 添加或改写一条常驻显示项
  - 显示在所有页面之上，包括存档、读档、设置、回想、音乐鉴赏、相册、消息框等自带页面
  - 没有焦点、不可点击、不参与输入；不随页面切换清除
  - 同一个 `id` 再次调用即改写文本或位置，不会新增第二条；`id` 可以是任意可作表键的值
  - `text` 内容变化时通过无障碍通道朗读；只改位置或颜色不会朗读；空字符串不朗读
  - `options` 字段：`x`、`y`（屏幕坐标，省略时在右上角自上而下堆叠）、`size`（字号，省略时按屏幕高度推算）、`color`（color表）、`silent`（为 `true` 时不朗读）
* `hud_remove(id: any)` - 移除一条常驻显示项

自带页面打开时脚本解释器暂停，但 `#lua` 协程与 `timer` / `every` 仍在运行，因此可以在任何页面上增删改常驻显示项；这些回调里不要假设脚本正在推进。

#### 改写界面条目

自带页面里的每一条都可以按“页面键 + 序号”改写文本、隐藏或追加，不改变游戏逻辑，也不写入存档。

* `item_set(page: string, index: int, text: string)`
  - 把第`index`条显示为`text`
  - `text`传空串表示撤销改写，恢复原文
  - 仅对运行时绘制文字的条目有效；纯图片条目（图片选项、相册缩略图）没有文字可改，会被忽略
  - 改写在关闭页面后依然保留
* `item_hide(page: string, index: int)`
  - 隐藏第`index`条，不再绘制，键盘导航会跳过它
  - 若隐藏后该页面已没有可选项，隐藏被忽略，避免菜单不可操作
  - 用`item_set`写一次非空文本即可取消隐藏
* `item_clear(page: string)`
  - 清除该页面的全部改写、隐藏和追加项
* `item_add(page: string, text: string) : int`
  - 在页面末尾追加一条文字条目，返回它的序号
  - 序号 = 该页的容量编号起点 + 它在追加列表中的位置；起点由引擎按页面登记：存读档`31`、设置`6`（Switch 且开启无障碍时`12`）、音乐鉴赏为曲目数、回想`64`、右键菜单`9`、脚本选项为候选数、消息框`3`、相册为当前页的 CG 数
  - 会真正显示出来的页面是设置、存档、读档、音乐鉴赏、回想、消息框、右键菜单与文本型选项菜单；右键菜单的背景框会随追加项一起放大
  - 相册是预先烘焙好的 5×5 图格，追加项不会出现
  - 列表页行高固定，追加项要滚动才能看到，不会压缩原有行
  - 追加项没有任何默认行为：必须用`on_item_click`给它注册回调，否则点击它不会有任何效果，既不报错，也不影响同一页的其他条目
* `on_item_click(page: string, callback: int -> void)`
  - 注册追加项的点击回调，`callback`收到被点击追加项的序号，与`item_add`的返回值一致
  - 同一页面重复注册会替换之前的回调
  - 没有注册回调时点击追加项不会有任何效果：消息框里没有回调的追加按钮不会关闭消息框，选择界面里也不会改动当前选中
  - 回调在帧循环里执行，与脚本同受看门狗约束
  - 点击追加项后，选择界面与右键菜单会像选中真实条目一样结束本次选择并重绘：要在回调里继续用这个菜单，请重新打开它，或用`cpymo.ui.exit()`把它关掉
* `item_on_click(page: string, index: int, callback: (string, int) -> bool?)`
  - 接管引擎自带条目的点击：`index`用该条目原本的编号，与`item_set`是同一套序号
  - `callback`收到`page`与被点击条目的序号
  - 返回`true`表示这次点击已被接管，引擎原本的动作整体跳过：设置项不再切换、存档位不再存档、曲目不再播放、回想条不再复制、相册不再打开预览
  - 不返回值、返回`false`、返回`nil`都表示先执行回调，再照常执行引擎原本的动作，适合只做记录、朗读或统计而不改变引擎行为
  - 重复注册替换之前的回调，`callback`传`nil`取消该条的回调
  - 只对真实条目生效；追加项仍然用`on_item_click`
  - 消息框里`1`是确定、`2`是取消，正文`0`不会被点击；同一页的页面键与序号见下表
  - 回调在帧循环里执行，与脚本同受看门狗约束

页面键与序号：

* `config` - 设置界面，序号与界面上从上到下的顺序一致
* `save`、`load` - 存档、读档界面，追加项序号从`CPYMO_MAX_SAVES`起
* `music` - 音乐鉴赏界面，序号为曲目在列表中的位置
* `backlog` - 回想界面，`0`是最早的一条
* `album` - 相册，序号为当前页内 CG 的位置（从左上到右下）
* `select` - 脚本选项与`cpymo.select_img`创建的选项菜单
* `msgbox` - 消息框，`0`正文、`1`确定、`2`取消，`3`起为追加按钮
* `rmenu` - 右键菜单，序号固定为 0 自动、1 存档、2 读档、3 快进、4 隐藏窗口、5 回想、6 设置、7 重开、8 返回游戏，`9`起为追加项，排在菜单末尾

查询：

* `item_get(page: string, index: int) : table?`
  - 返回`{index, text, hidden, extra, extra_index, rewritten}`；序号落在该页容量内或是一条追加项时都能查到
  - 完全没有改写时也返回该条现在的内容：`text`取自引擎实际绘制的文本，该条还没被绘制过时为`nil`
  - `rewritten`为`true`表示这一层存有该条的改写或隐藏；`hidden`只对真实条目有意义
  - `extra_index`是追加项在追加列表中的位置，真实条目为`-1`
  - 序号既不是真实条目也不是追加项时返回`nil`
* `items(page: string) : table`
  - 返回`{page, base, count, extra_count, entries, extras}`
  - `base`是该页的容量编号起点（该页面还没被打开过时为`nil`），`count`是被改写的真实条目数，`extras`是该页全部的追加项
  - `entries`列出该页的全部真实条目，没被改写的也在其中；每项含`index`、`text`、`hidden`与`rewritten`，没有改写时`text`取自引擎实际绘制的文本
* `pages() : string[]` - 返回全部页面键

#### 保留与持久化

改写在内存里，跟随引擎生命周期：关闭页面、切换场景、重进脚本都不会丢，但引擎不会自动把它写进存档——是否持久化、存在哪里由开发者决定。引擎只提供纯粹的导出与导入：

* `export_patch() : table` - 把当前全部改写与追加导出成纯数据表`{entries = {...}, bases = {...}}`，里面只有数字、字符串与布尔值
* `import_patch(t: table)` - 用表的内容替换当前全部改写（先清空再写入，不是合并）

表就是普通 Lua 表，可以直接交给引擎自带的序列化函数：

```lua
local text = cpymo.serialize(cpymo.ui.export_patch())   -- 变成一个字符串
cpymo.ui.import_patch(cpymo.deserialize(text))          -- 读回成表并应用
```

要跟着存档走，就在主 actor 的`save` / `load`里各加一行——引擎存的就是`save()`的返回值：

```lua
function save()
    return { ui_patch = cpymo.ui.export_patch() }
end

function load(t)
    if t and t.ui_patch then cpymo.ui.import_patch(t.ui_patch) end
end
```

要写进独立文件，用 Lua 的文件接口（宿主的 Lua 打开了`io`库时）：

```lua
local f = io.open("ui_patch.txt", "w")
f:write(cpymo.serialize(cpymo.ui.export_patch()))
f:close()
```

查询用的原文缓存只是引擎绘制时的记录，不属于改写数据，不参与`export_patch()`。

### `cpymo.select_img`

把引擎内建的图片选择界面（脚本里的`select_img`/`select_imgs`命令）交给Lua驱动：Lua给出候选、位置与可用状态，界面本身仍由引擎绘制与操作，因此焦点移动的提示音、朗读、鼠标悬停与存档标记等行为与脚本路径完全一致。

* `begin(filename: string, count: int)`
  - 开始配置一次选择，`filename`为`system`目录下的选项图文件名，`count`为候选数量
  - 已有一个未结束的选择界面时返回`CPYMO_ERR_INVALID_ARG`
* `select(x: number, y: number, enabled: bool [, hash: int])`
  - 追加一个候选，必须恰好调用`begin`给出的`count`次
  - `x`/`y`为选项图左上角的位置，`enabled`为`false`的候选不会显示
  - `hash`用于记录“该选项曾被选过”，省略时按`0`处理。Lua无法复现脚本侧由命令名、序号与文件名算出的hash，因此这条路径与脚本路径的“已选过”标记不互通，需要时请自行约定固定值
* `finish(init_pos: int [, callback: function])`
  - 结束配置并进入等待；候选数量与`begin`不一致时返回`CPYMO_ERR_INVALID_ARG`
  - `init_pos`为初始焦点序号；传`-1`表示允许玩家在等待中滚轮查看回想、右键打开菜单并对此选择存档，与脚本命令传`-1`等价
  - `callback(sel: int, hash: int, save_enabled: bool)`在选择被确认后调用，参数是引擎写回的信息
* `active() : bool` - 当前是否有选择界面在配置或等待
* `current() : int` - 当前焦点序号，没有界面时为`-1`
* `cancel()` - 丢弃当前界面；若正在等待，等待会立即结束，脚本继续向下执行

确认选择时引擎先执行默认行为（播放确认音、按`save_enabled`记录hash、把序号写入PyMO变量`FSEL`），再调用`callback`。因此带`callback`与不带时的变量与存档行为相同，只是多了一次回调。

`finish`之后不必在Lua里绘制或推进这个界面：引擎主循环无条件更新并绘制它，Lua只需在`callback`里处理自己的后续逻辑。

### `cpymo.flags`

该包内的功能用于管理Flag，每个Flag由一个字符串组成，可检查其存在或不存在。
这些Flag是全局的，将会在全局存档中保存。
这里的Flag最终使用一个Hash值表示，不保证可靠。

* `set(flag: string)` - 设置一个flag
* `check(flag: string) : bool` - 检查这个flag是否存在
* `unset(flag: string)` - 取消一个flag
* `unlock_cg(cg_name: string)`
  - 解锁一个CG，不带扩展名
  - 仅`gameconfig.txt`中符合`cgprefix`条件的图片可被解锁
* `lock_cg(cg_name: string)` - 重新锁定一个CG，不带扩展名
* `cg_unlocked(cg_name: string) : bool` 检查这个CG是否被锁定


### `cpymo.input`

* `mouse_moved() : bool` - 当鼠标移动时返回`true`

#### `cpymo.input.now`
该包不可使用pairs遍历，表示当前帧引擎所获取的输入。

其中以下值为`bool`类型：

* `up`, `down`, `left`, `right` - 方向键
* `ok` - 确定键
* `cancel` - 取消键/鼠标右键
* `skip` - 跳过键
* `hide_window` - 隐藏对话框键
* `mouse_button` - 鼠标左键/触摸屏幕是否按下
* `copy` - 复制键（读屏辅助按键，可能由无障碍手势触发）
* `append_copy` - 追加复制键（读屏辅助按键，可能由无障碍手势触发）
* `mouse_position_useable` - 鼠标指针坐标是否可用/触摸屏坐标是否可用

以下值为`number`类型：

* `mouse_x`, `mouse_y` - 鼠标坐标/触摸坐标，仅在`mouse_position_useable`为`true`时可用
* `mouse_wheel_delta` - 鼠标滚轮在上一瞬间滑动量

#### `cpymo.input.prev`

该包内存放了上一帧的输入，与`cpymo.input`内容相同。

#### `cpymo.input.just_pressed`

该包内存放了所有刚刚按下的按钮，包括上述`cpymo.input.now`中所有bool类型的值。

#### `cpymo.input.just_released`

该包内存放了所有刚刚松开的按钮，包括上述`cpymo.input.now`中所有bool类型的值。

### `cpymo.a11y`

无障碍支持入口，用于让Lua自建的界面接入读屏、提示音与振动。

仅在启用无障碍文本导出的构建（`ENABLE_TEXT_EXTRACT`）中真正生效；其它构建中三个函数都是静默无效的空操作，因此脚本可以无条件调用，不需要自行判断平台。

* `speak(string)` - 朗读一段文本，使用与游戏正文完全相同的通道，打断、去重与排队规则与正文一致
* `sound(type)` - 播放一次无障碍提示音，`type`取`cpymo.a11y.SOUND_ENTER`、`cpymo.a11y.SOUND_MENU`或`cpymo.a11y.SOUND_SELECT`
* `vibrate(milliseconds)` - 振动指定毫秒数，取值`1`至`1000`；平台不支持时静默忽略

常量：`SOUND_ENTER`(1) 进入新界面、`SOUND_MENU`(2) 打开或关闭菜单、`SOUND_SELECT`(3) 焦点移动。

注意：`cpymo.render`绘制的文本不会被引擎自动朗读。Lua自建的界面必须在内容变化或焦点改变时自行调用`cpymo.a11y.speak`；引擎原生界面（存读档、设置、回想、音乐鉴赏等，通过`cpymo.ui`打开）已经自带朗读。

### `cpymo.script`

该包用于与PyMO脚本解释器交互：

* `vars` - 这个表包含了所有的PyMO变量，仅可在其中读写整数，不可遍历
* `commands` - 这个表包含了所有新增或重载的PyMO脚本命令，可以在这里覆盖原有PyMO命令或添加新的脚本命令
  - 所有的PyMO参数都会以字符串传入，未匹配成功的参数将以nil传入
  - 如重载`bg`命令，则需要重载为`function cpymo.script.commands.bg(name, transition, time, x, y)`，所有的参数均为字符串，且返回值无意义
  - 如果调用重载命令后，引擎判断不需要刷新帧，则会立刻执行下一条命令，如果需要刷新帧（如调用了`cpymo.request_redraw()`或产生了其他引擎认为需要刷新的情况），
  - 则将会在下一帧继续执行后面的PyMO代码
  - 当你重载了PyMO命令之后，将不再可以执行原有的PyMO命令
* `push_code(pymo_code: string)` - 将PyMO代码压入PyMO调用栈中，在下次PyMO解释器执行时执行PyMO代码`pymo_code`
* `wait(finished: (delta_time: number) -> bool, finish_callback: () -> ())`
  - 将会使得PyMO解释器每帧执行一次`finished`，参数为已经经过的时间
  - 在其返回`true`之前不会继续工作，
  - 一旦`finished`返回`true`，则会调用`finish_callback`，并继续执行后面的PyMO代码

### `cpymo.save`

* `save_global()` - 立刻保存全局存档
