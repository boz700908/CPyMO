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
