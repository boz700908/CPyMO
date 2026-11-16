# CPyMO 指令参考（PyMO / YukimiScript 双写法）

本文按引擎实际分派的 **74 条命令**逐条给出两种写法：

- **PyMO 脚本**：`.pymo` 文本，行首 `#命令名`，参数用**半角逗号**分隔，一行一条指令。
- **YukimiScript**：`.ykm` 文本，行首 `@命令名`，参数用**空格**分隔，可选参数可写成 `--参数名 值`。

内容取自 `PYMO_Manual_v1.2.0.md` 第 12 章与仓库根的 `libpymo.ykm`（ykm 侧的参数名与默认值以该文件的
`extern`/`macro` 声明为准），并用一份覆盖全部命令的测试脚本在引擎上逐条核对过（见文末「覆盖与验证」）。

---

## 0. 两种写法速览

| 项目 | PyMO | YukimiScript |
| --- | --- | --- |
| 指令前缀 | `#` | `@` |
| 参数分隔 | 逗号，一行写完全部参数 | 空格，可拆成多行 |
| 字符串 | 不带引号的裸文本 | 双引号 `"文本"`，可 `$"..."` 插值 |
| 颜色 | `#RRGGBB` | `0xRRGGBB`，或 `white` `black` `red` `green` `blue` |
| 坐标模式 | `0`~`6` | `cm0`~`cm6` |
| 变量/符号 | 裸名（`F11`、`FSEL`） | 裸名即符号，字符串要引号 |
| 条件 | `#if F11=0, goto L` | `@if F11 eq 0` … `@elif` … `@else` … `@endif` |
| 标签 | `#label L` / `#goto L` | `- scene "L"` / `@goto "L"` |
| 说话 | `#say 名字,正文` | 文本行 `符号:正文`（无名旁白直接写正文） |
| 注释 | `;` 开头 | `#` 开头 |

ykm 侧的另外两条规则：

- **参数个数可变的命令一律拆成「收集行 + `_do` 收尾行」**。例如 `chara_multi` 收集每个角色，
  最后 `chara_multi_do` 才统一生效；只写收集行不会有任何动作。
- 默认值写在 ykm 声明的 `=` 后面，省略即取默认。文档里凡写 `[方括号]` 的都是可省略参数。

> **反过来在 PyMO 侧没有 `_do` 这回事**：参数个数可变的命令把全部参数塞进一行、用逗号连写完，最后跟的那个数字就是时长/收尾值（例：`#chara 0,A,25,0,400`）。把它拆成「收集行 + 纯数字行」在引擎里是两条独立命令，第二条没有对象可作用，什么也不会做。引擎的 `POP_ARG` 只读当前行，不跨行。

> **ykm 里没有 `say` 与 `label`**：`libpymo.ykm` 不提供这两条。前者用 YukimiScript 的文本语法，
> 后者由 scene 承担——`#label L` 在 ykm 侧就是 `- scene "L"`，`#goto L` 就是 `@goto "L"`。
> `pymo2ykm.ps1` 做同样的转换：一条 `#label L` 产出 `@goto "L"` 加一个 `- scene "L"`。

---

## 1. 文本与对话

### `say` — 人物对话

- PyMO：`#say [name,] content`。`name` 省略时只有正文；`content` 可写 `\n` 换行，不要含半角逗号。
- ykm：无此命令，直接写文本行。`符号:正文` 表示带名字，正文单独一行表示旁白；符号须先用
  `@__define_character` 绑定到名字。

```
#say 智也,「…久等了……」
#say 她用甜甜的笑脸，\n回应着我的话。
```
```
@__define_character a "智也"
a:「…久等了……」
她用甜甜的笑脸，\n回应着我的话。
```

同一名字务必每次写成完全相同的字符串，否则会被当成两个角色，对话记录里的名字也会分裂。

### `text` — 屏幕文字

- PyMO：`#text content,x1,y1,x2,y2,color,size[,show_immediately]`，不记入对话记录与存档。
- ykm：`@text content x1 y1 x2 y2 [--color 0xFF0000] [--size 16] [--show_immediately true]`。

```
#text 第一章,10,10,50,50,#FF0000,16,1
```
```
@text "第一章" 10 10 50 50 0xFF0000 16 true
```

### `text_off` — 清除屏幕文字

- PyMO：`#text_off`　ykm：`@text_off`

### `waitkey` — 等待按键

- PyMO：`#waitkey`　ykm：`@waitkey`
- 5 秒无按键自动继续。

### `title` — 设置章节标题

- PyMO：`#title content`　ykm：`@title "序章"`
- 只设置，不显示；该标题会作为存档标题。

### `title_dsp` — 显示章节标题

- PyMO：`#title_dsp`　ykm：`@title_dsp`

### `intro` — 无对话框引言（CPyMO 扩展）

- PyMO：`#intro content`　ykm：`@intro "那是很久以前的事了。"`
- 文字排在屏幕中部（左右各 10%，纵向 25%~75%），照常逐字显示、等待推进、记入对话记录、触发自动存档。
- 只对当前这一句生效，下一句 `#say` 恢复对话框与名字框；`#intro` 期间不绘制 `#avatar`。
- 用「继续」键推进（ascii-art 后端是 `J` 或空格），用「取消」键只会取消这一句。

```
#intro 那是很久以前的事了。\n\n那个夏天，我第一次遇见了她。
```
```
@intro "那是很久以前的事了。\n\n那个夏天，我第一次遇见了她。"
```

### `filmmode` — 电影画幅（CPyMO 扩展）

- PyMO：`#filmmode on|off`　ykm：`@filmmode on`（`on` 之外任何值都是关闭）
- 黑边画在所有内容之上，不改变对话框与立绘布局；不写存档，读档后为关闭。

### `avatar` — 对话框小头像（CPyMO 扩展）

- PyMO：`#avatar name` / `#avatar`（关闭）
- ykm：`@avatar "face_smile"` / `@avatar` 或 `@avatar_off`
- 图片放在 `system/` 下，不带扩展名；贴对话框左边、底边对齐，高度为对话框高度的 0.8 倍。
  加载失败只警告，脚本继续。不写存档。

### 富文本内联标记（CPyMO 扩展）

在 `say`、`intro`、`text` 的正文里就地染色或注音，两种写法通用：

- `[文字](style=color~#RRGGBB)`：只染方括号内那段。
- `[汉字](ruby=かんじ)`：括号里的读音被剥离，既不上屏也不进对话记录。

键名必须精确、值不能为空、颜色必须 `#` 开头；任何一项不满足就整段连同方括号原样显示，不报错。

```
#say 她把[Crimson](style=color~#DC143C)色的围巾系好。
```
```
a:她把[Crimson](style=color~#DC143C)色的围巾系好。
```

---

## 2. 图像

### `chara` — 显示立绘

- PyMO：`#chara id1,file1,pos1,layer1[,id2,file2,pos2,layer2…],time`，已有同 ID 立绘被替换，
  `filename` 写 `NULL` 表示擦除；`time` 是淡入毫秒数。
- ykm 单角色：`@chara id "file" [position=50] [layer=0] [time=300]`（宏，展开成 `#chara`）。
- ykm 多角色：每角色一行 `@chara_multi id "file" [position] [layer]`，末尾 `@chara_multi_do [--time 300]`。

```
#chara 0,SM02AMA,25,0,400
#chara 0,SM02AMA,25,1,1,SN01AMA,75,2,500
```
```
@chara 0 "SM02AMA" 25 0 400

@chara_multi 0 "SM02AMA" 25 1
@chara_multi 1 "SN01AMA" 75 2
@chara_multi_do --time 500
```

### `chara_cls` — 擦除立绘

- PyMO：`#chara_cls id[,time]`，`id` 写 `a` 清除全部。
- ykm：`@chara_cls [id=a] [time=300]`

```
#chara_cls a
#chara_cls 0,500
```
```
@chara_cls a 500
```

### `chara_pos` — 更改立绘位置

- PyMO：`#chara_pos id,new_x,new_y,coord_mode`
- ykm：`@chara_pos id new_x new_y [coord_mode=cm5]`

### `bg` — 加载背景

- PyMO：`#bg file[,transition,time[,x,y]]`
- ykm：`@bg "file" [transition=BG_ALPHA] [time=300] [x=0] [y=0]`
- `transition` 取值 `BG_NOFADE`（无渐变）、`BG_ALPHA`（Alpha 淡入，默认）、`BG_FADE`（淡出成黑再淡入），
  或 `system/` 下的遮罩图片名。加载背景会清除当前立绘。

```
#bg BG001_H, BG_FADE, 500, 10, 20
```
```
@bg "BG001_H" BG_FADE 500 10 20
```

### `flash` — 屏幕闪光

- PyMO：`#flash color,time`　ykm：`@flash [color=white] [time=300]`

### `quake` — 画面振动

- PyMO：`#quake`　ykm：`@quake`

### `fade_out` — 屏幕淡出

- PyMO：`#fade_out color,time`　ykm：`@fade_out [color=black] [time=300]`
- 一定要配合 `fade_in`；淡出期间对背景与立绘的操作不可见，正好用来在后台换景。

### `fade_in` — 屏幕淡入

- PyMO：`#fade_in time`　ykm：`@fade_in [time=300]`

### `movie` — 播放视频

- PyMO：`#movie file`　ykm：`@movie "file"`

### `textbox` — 更换对话框图片

- PyMO：`#textbox message,name`　ykm：`@textbox [message="message"] [name="name"]`

### `chara_quake` — 立绘左右振动

- PyMO：`#chara_quake id1[,id2…]`
- ykm 单 ID：`@chara_quake id`；多 ID：每个一行 `@chara_quake_multi id`，末尾 `@chara_quake_multi_do`。

```
#chara_quake 0,1
```
```
@chara_quake_multi 0
@chara_quake_multi 1
@chara_quake_multi_do
```

### `chara_down` — 立绘下沉

- PyMO：`#chara_down id1[,id2…]`　ykm：`@chara_down id` 或 `@chara_down_multi id` × N + `@chara_down_multi_do`

### `chara_up` — 立绘上跳

- PyMO：`#chara_up id1[,id2…]`　ykm：`@chara_up id` 或 `@chara_up_multi id` × N + `@chara_up_multi_do`

### `scroll` — 滚动图片

- PyMO：`#scroll file,startx,starty,endx,endy,time`
- ykm：`@scroll "file" startx starty endx endy [time=300]`
- 会清除当前立绘。

### `chara_y` — 指定横纵坐标显示立绘

- PyMO：`#chara_y coord_mode,id1,file1,x1,y1,layer1[,…,id2,file2,x2,y2,layer2…],time`
- `coord_mode` 0~6 决定 x/y 含义（左沿/中点/右沿 距屏幕左沿，上沿/中点/下沿 距屏幕上沿）：

| 模式 | x | y |
| --- | --- | --- |
| 0 | 左沿距左 | 上沿距上 |
| 1 | 中点距左 | 上沿距上 |
| 2 | 右沿距右 | 上沿距上 |
| 3 | 中点距左 | 中点距上 |
| 4 | 左沿距左 | 下沿距下 |
| 5 | 中点距左 | 下沿距下 |
| 6 | 右沿距右 | 下沿距下 |

- ykm 单角色：`@chara_y coord_mode id "file" x y [layer=0] [time=300]`（coord_mode 写 `cm0`~`cm6`）。
- ykm 多角色：`@chara_y_multi id "file" x y [layer]` × N，末尾 `@chara_y_multi_do [--coord_mode cm5] [--time 300]`
  ——模式与时间只出现在收尾行。

```
#chara_y 1,0,SM02AMA,25,10,0,400
```
```
@chara_y_multi 0 "SM02AMA" 25 10 0
@chara_y_multi_do --coord_mode cm1 --time 400
```

### `chara_scroll` — 立绘滑动淡入

- PyMO 10 参：`#chara_scroll coord_mode,id,file,startx,starty,endx,endy,beginalpha,layer,time`
- PyMO 5 参：`#chara_scroll coord_mode,id,endx,endy,time`（已存在的立绘滑到新位置）
- ykm 10 参：`@chara_scroll_complex coord_mode id "file" startx starty endx endy [beginalpha=0] [layer=0] [time=300]`
- ykm 5 参：`@chara_scroll coord_mode id endx endy [time=300]`
- `beginalpha` 0 不透明、255 完全透明。两者在引擎里是同一套实现，`chara_scroll_complex` 只是 10 参形态的名字。

```
#chara_scroll 5,0,SM02AMA,0,0,50,0,130,1,400
#chara_scroll 5,0,100,0,400
```
```
@chara_scroll_complex cm5 0 "SM02AMA" 0 0 50 0 130 1 400
@chara_scroll cm5 0 100 0 400
```

### `anime_on` — 播放动画

- PyMO：`#anime_on num,file,x,y,interval,isloop`（六格缺一个即判脚本错误）
- ykm：`@anime_on num "file" [x=0] [y=0] [interval=500] [loop=false]`
- 图片放 `system/` 下；同屏只能存在一个动画；绘制层级在立绘之上、对话框与选择界面之下。
- 只有 `isloop` 为 1 的动画写进存档，读档后接着播。`LOW_FRAME_RATE` 构建下整条跳过。

```
#anime_on 3,rain,0,0,300,1
```
```
@anime_on 3 "rain" 0 0 300 true
```

### `anime_off` — 停止动画

- PyMO：`#anime_off [file]`　ykm：`@anime_off ["file"]`
- 引擎不读取参数，写成不带参数效果相同。

### `chara_anime` — 自定义立绘震动

- PyMO 单行写法：`#chara_anime id, period, loop_num, offset_x1,offset_y1, offset_x2,offset_y2, …`
- ykm（也是引擎真正识别的形态）：每行 `@chara_anime offset_x offset_y` 收集一组偏移（上限 32 组），
  末尾 `@chara_anime_do id [period=500] [loop_num=1]` 才播放。只写收集行不会有任何动作，
  而 `chara_anime_do` 前面没有任何偏移行时会报 `chara_anime_do has no offset` 并跳过该行。
- `loop_num`：`0` 停止、正数播 N 轮、负数无限循环。`period` 为 0 或负数时立即结束，不会卡住脚本。
- 阻塞是默认行为（脚本停在 `chara_anime_do`）；`loop_num` 为负数时一直播到玩家按「继续」。
- 后台循环：在 `chara_anime_do` 末尾再补一个参数（填 `1` 或 `bg`），脚本不停，动画后台无限循环，
  停止方式是 `chara_anime_do id period 0` 或用下一个动画覆盖。**注意 `libpymo.ykm` 只声明了三个参数，
  手写 ykm 想用后台循环需要给 `chara_anime_do` 补上第四个参数声明**（转换器产出的 `#chara_anime` 单行写法
  由引擎按 4 参形态解析，不受此限）。
- `LOW_FRAME_RATE` 构建下整条跳过。

```
#chara_anime 1, 100, 2, 0,7, 0,16, 0,12
#chara_anime 1, 100, -1, 0,3, 0,-3
```
```
@chara_anime 0 7
@chara_anime 0 16
@chara_anime 0 12
@chara_anime_do 1 100 2

@chara_anime 0 3
@chara_anime 0 -3
@chara_anime_do 1 100 -1
```

### `chara_scale` — 立绘缩放（CPyMO 扩展）

- PyMO：`#chara_scale id,scale[,time]`　ykm：`@chara_scale id scale [time=0]`
- 以立绘自身中心为锚点，位置与图层不变；`time` 大于 0 时等过渡结束再往下走；`scale` 恰为 1.0 时跳过计算。

```
#chara 1,hero,50,0,300
#chara_scale 1,1.15,400
```
```
@chara 1 "hero" 50 0 300
@chara_scale 1 1.15 400
```

---

## 3. 变量、选择、跳转

### 系统特殊变量

- `FSEL`：`#sel` 与 `#select_text` 的选择结果（按整体选项标号，未显示的选项也占号）。
- `FMONTH` / `FDATE`：游戏内月份与日期，供 `#date` 使用。
- 以字母 `S` 开头的变量是全局变量，存在 `golbal.sav`，不受读档影响。

### `set` / `add` / `sub` — 变量赋值与加减

- PyMO：`#set var,value`、`#add var,value`、`#sub var,value`（value 可以是数字或变量名）
- ykm：`@set var val`、`@add var val`、`@sub var val`

```
#set F58,FSEL
#add F11,1
```
```
@set F58 FSEL
@add F11 1
```

### `label` — 行标签

- PyMO：`#label name`（同脚本内不要重名）
- ykm：`- scene "name"`。scene 既是标签也是编译单位，`@goto "name"` 跳过去。

### `goto` — 跳转

- PyMO：`#goto name`　ykm：`@goto "name"`

### `if…goto` — 条件跳转

- PyMO：`#if 左操作数 运算符 右操作数, goto label`，运算符支持 `=` `>` `<` `>=` `<=` `!=`；
  左操作数必须是变量，右操作数可以是变量。
- ykm：`@if_goto left op right "label"`，`op` 写 `eq` `ne` `gt` `ge` `lt` `le`；
  另有 `@if_sel_goto 编号 "label"`（等价于 `FSEL eq 编号`）。

```
#if F11=0, goto SEL_LABEL_0
#if F11>=S1, goto SEL_LABEL_1
```
```
@if_goto F11 eq 0 "SEL_LABEL_0"
@if_goto F11 ge S1 "SEL_LABEL_1"
```

ykm 另有一套结构化条件（不需要标签）：

```
@if F11 eq 0
    @quake
@elif F11 eq 1
    @flash red 200
@else
    @bg "BG002_H"
@endif
```

`if`/`elif`/`else`/`endif` 是 ykm 自己的语句，编译期展开，不是引擎命令。

### `change` — 切换脚本（无返回）

- PyMO：`#change file`　ykm：`@change "file"`

### `call` — 调用脚本（带返回）

- PyMO：`#call file`　ykm：`@call "file"`

### `ret` — 脚本返回

- PyMO：`#ret`　ykm：`@ret`。被 `#call` 的脚本末尾必须写。

### `sel` — 选择肢（多行）

- PyMO 必须多行写：第一行 `#sel 选项数[,提示图]`，随后每行一个选项文本；写成单行会转换失败。
- ykm：每个选项一行 `@sel "文本"`，末尾 `@sel_do ["提示图"]`。收尾行才真正弹出菜单。
- 结果写入 `FSEL`。相比 `select_text`，`sel` 支持选择中存档、已选项变灰与动画提示。

```
#sel 2
去放焰火吧
到我家里去坐坐吧？
```
```
@sel "去放焰火吧"
@sel "到我家里去坐坐吧？"
@sel_do
```

### `select_text` — 文本选择菜单

- PyMO：`#select_text 数量,文本1,文本2,…,x1,y1,x2,y2,color,init_position`
- ykm：每项一行 `@select_text "文本"`，末尾
  `@select_text_do x1 y1 x2 y2 [--color 0x409900] [--init_position 0]`（六格全必填）。

```
#select_text 3,开始游戏,继续游戏,退出,0,50,100,100,#409900,0
```
```
@select_text "开始游戏"
@select_text "继续游戏"
@select_text "退出"
@select_text_do 0 50 100 100 0x409900 0
```

### `select_var` — 由变量控制显示的选择菜单

- PyMO：`#select_var 数量,文本1,var1,文本2,var2,…,x1,y1,x2,y2,color,init_position`
  （变量为 0 的选项不显示，但仍占 `FSEL` 的编号；`init_position` 设为 -1 时可用「已选项变灰」与「选择中存档」）
- ykm：`@select_var "文本" [enabled=1]` × N + `@select_var_do x1 y1 x2 y2 color init_position`

```
#select_var 2,去追她,S1,回家,S2,0,50,100,100,#FFFFFF,-1
```
```
@select_var "去追她" S1
@select_var "回家" S2
@select_var_do 0 50 100 100 0xFFFFFF -1
```

### `select_img` — 图形选择菜单（单图）

- PyMO：`#select_img 数量,file,x1,y1,var1,x2,y2,var2,…,init_position`
  图片放 `system/` 下，**图名不带扩展名**，分左右两列（左未选中、右选中）。
- ykm：`@select_img x y [enabled=1]` × N + `@select_img_do "file" [init_position=0]`
  ——共享的图名只出现在收尾行。

```
#select_img 2,menu,0,0,S1,0,50,S2,0
```
```
@select_img 0 0 S1
@select_img 0 50 S2
@select_img_do "menu" 0
```

### `select_imgs` — 图形选择菜单（多图）

- PyMO：`#select_imgs 数量,file1,x1,y1,var1,file2,x2,y2,var2,…,init_position`
- ykm：`@select_imgs "file" x y [enabled=1]` × N + `@select_imgs_do [init_position=0]`

```
#select_imgs 2,item1,0,0,S1,item2,0,50,S2,0
```
```
@select_imgs "item1" 0 0 S1
@select_imgs "item2" 0 50 S2
@select_imgs_do 0
```

### `wait` — 等待

- PyMO：`#wait time`（毫秒）　ykm：`@wait [time=300]`

### `wait_se` — 等待音效结束

- PyMO：`#wait_se`　ykm：`@wait_se`。按确定键可中断等待。

### `rand` — 随机数

- PyMO：`#rand var,min,max`　ykm：`@rand var min max`

### `input` — 数字输入（CPyMO 扩展）

- PyMO：`#input var[,prompt]`　ykm：`@input var ["prompt"]`
- 只接受数字、首字符负号、退格、回车确认、ESC 取消；最多 16 字符；输入为空按 0 写入；
  ESC 取消时变量保持原值。后端不支持文本输入时打印一条警告并立即结束，不会卡住脚本。

```
#input passcode,请输入四位数
#if passcode=1234, goto right
```
```
@input passcode "请输入四位数"
@if_goto passcode eq 1234 "right"
```

---

## 4. 声音

### `bgm` — 播放背景音乐

- PyMO：`#bgm file[,isloop]`（默认循环）　ykm：`@bgm "file" [loop=true]`
- 已有 BGM 会先停止再播放新的。

### `bgm_stop` — 停止背景音乐

- PyMO：`#bgm_stop`　ykm：`@bgm_stop`

### `se` — 播放音效

- PyMO：`#se file[,isloop]`（默认不循环）　ykm：`@se "file" [loop=false]`

### `se_stop` — 停止音效

- PyMO：`#se_stop`　ykm：`@se_stop`

### `vo` — 播放语音

- PyMO：`#vo file`　ykm：`@vo "file"`

---

## 5. 系统

### `load` — 读档

- PyMO：`#load [save_num]`　ykm：`@load [id=null]`
- 不指定存档号则调出读档界面；指定则直接读该存档。

### `album` — CG 鉴赏

- PyMO：`#album [列表文件名]`　ykm：`@album ["列表文件名"]`
- 列表脚本默认名 `album_list`，每行 `页码,该页缩略图数,CG名,bg名…`，页码从 1 开始，缩略图从 `bg/` 加载；
  鉴赏页背景用的 UI 脚本默认名 `albumbg`。列表脚本缺失时页面加载失败并报
  `Script file not found.`。

### `music` — 音乐鉴赏

- PyMO：`#music`　ykm：`@music`
- 列表脚本 `music_list`，每行 `音乐文件名,标题`（第一格必须非空）；数量为 0 时菜单直接结束。

### `date` — 显示游戏日期

- PyMO：`#date bg,x,y,color`　ykm：`@date "bg" x y [color=white]`
- 背景图放 `system/` 下；日期取自 `FMONTH` 与 `FDATE`。

```
#date EYE_D,65,42,#000000
```
```
@date "EYE_D" 65 42 0x000000
```

### `config` — 游戏设置面板

- PyMO：`#config`　ykm：`@config`

---

## 6. Lua（Feature Level 1）

### `lua` — 执行 Lua 代码（CPyMO 扩展）

- PyMO：`#lua lua_code`　ykm：`@lua "lua_code"`
- 行内 `#lua` 之后的**整行剩余内容**都是代码，不再按逗号切分、不把分号当注释；多条语句用 `;` 分隔。
  字符串里的换行必须写 `\n`，真实折行会被当成下一条指令。
- 所有 `#lua`、`@lua` 与游戏共用同一个 Lua 状态，全局变量跨指令保留。
- 块可以挂起：`cpymo.wait(秒)`、`cpymo.yield()`；挂起期间游戏照常运行。
- 阻塞调用被拦下并报错：`io.read`、无参 `io.lines`、`io.popen`、`os.execute`、`debug.sethook`。
- 一条块还没跑完、脚本又走到下一条 `#lua` 时，未完成的旧块被丢弃。
- 只有 Feature Level 1 构建支持；Level 0 构建与原版 pymo 打印警告后跳过该行。

```
#lua cpymo.a11y.speak("这是一句朗读")
```
```
@lua "cpymo.a11y.speak(\"这是一句朗读\")"
```

---

## 7. 收尾命令一览

以下命令本身不产生动作，只把此前若干条收集行的参数一次性生效。ykm 里遗漏收尾行是最常见的「写了没反应」原因。

| 收集行 | 收尾行 |
| --- | --- |
| `chara_multi` | `chara_multi_do` |
| `chara_quake_multi` | `chara_quake_multi_do` |
| `chara_down_multi` | `chara_down_multi_do` |
| `chara_up_multi` | `chara_up_multi_do` |
| `chara_y_multi` | `chara_y_multi_do` |
| `chara_anime` | `chara_anime_do` |
| `sel` | `sel_do` |
| `select_text` | `select_text_do` |
| `select_var` | `select_var_do` |
| `select_img` | `select_img_do` |
| `select_imgs` | `select_imgs_do` |

PyMO 侧不需要手写收尾行：`pymo2ykm.ps1` 会把单行语法展开成「收集行 + 收尾行」。

---

## 8. 转换器对照

`pymo2ykm.ps1` 把 `.pymo` 转成 `.ykm` 时的主要改写：

- 带名字的 `#say` 首次出现时注册角色符号（`@__define_character c0 "名字"`），正文改用符号；
  未注册的名字不产生动作。
- 多角色形态的 `#chara`、`#chara_y`、`#chara_quake`、`#chara_down`、`#chara_up`、`#chara_anime`、
  `#sel`、四个 `#select_*` 展开成收集行加收尾行。
- `#if X>=Y, goto L` 展开成 `@if_goto X ge Y "L"`。
- `#label L` 展开成 `@goto "L"` 加 `- scene "L"`。
- 正文里的 `[ ] < >` 默认换成全角，但富文本标记 `[文字](style=color~#RRGGBB)` 与
  `[汉字](ruby=…)` 会先被识别并原样保留。
- 无法识别的命令降级成注释，不会报错——所以转换后请核对脚本里没有本该执行、结果变成注释的行。

---

## 9. 覆盖与验证

本文的命令集与引擎分派表一致（74 条：56 条基础命令 + 18 条多角色族与 `*_do` 收尾形态），
并用一份测试游戏在 ascii-art 后端上逐条执行过：非阻塞命令一次性跑完并断言状态，阻塞命令
（`say`、`waitkey`、`wait_se`、`text`、`input`、`sel` 与 `select_*`、`album`、`music`、`config`、`intro`、`load`）
各自在独立目录里用伪终端喂键跑通，全部无 `[Error]`。

几处只在运行时才暴露的前提，写脚本时容易踩：

- `select_img_do` / `select_imgs_do` 的图片从 `system/` 解析，写 `bg/` 下的名字会报 `Can not open file.`。
- `album` 必须同时准备 `album_list` 与鉴赏页 UI 脚本（如 `albumbg`），`music` 必须有 `music_list`。
- `anime_on` 的图片必须是 `system/` 下的竖条多帧图。
- `#intro` 用「继续」键推进；「取消」键只取消这一句，不会让脚本继续往下走。
- 测试时若残留旧的 `config.csav`，它会覆盖 `gameconfig.txt` 里的 `fontsize`，表现为字号异常小。
