# CPyMO 开发与移植指南

这份文档写给两种人：

* 想从零写一个 CPyMO 游戏的人（或 AI）。
* 手上有一份老 PyMO 游戏，想把它搬到 CPyMO 上跑的人。

两者的第一步是一样的：先搞清楚一个游戏目录里有什么。所以从第 1 节顺序读下去即可；只想移植的可以直接跳第 9 节，但第 2 节（`gameconfig.txt`）仍然必须读。

单条命令的参数、`#命令` 与 `@命令` 的对照，见 [CPyMO 指令参考.md](CPyMO%20指令参考.md)。本文只讲「怎么把它们组装成一个游戏」。

本文出现的每条结论都标了来源：

* **引擎**：能在 CPyMO 源码里指到具体行为，可信。
* **手册**：来自 `PYMO_Manual_v1.2.0.md`（PyMO 时代的中文手册）。PyMO 是 Python/S60 上的老引擎，CPyMO 重新实现了它但并非逐字节兼容，手册里过时的地方本文会标注。
* **推测**：没有直接证据，只是合理猜测。看到这三个字请自己验证一遍。

---

## 0. 先选一条路

| 你的情况 | 走哪条 | 从哪读 |
| --- | --- | --- |
| 全新游戏，愿意写 YukimiScript | 写 `.ykm`，用 `ykmc` 编译成 `script/*.txt` | 第 4 节 |
| 全新游戏，想直接手写脚本 | 直接写 PyMO 风格的 `#命令` 文本 | 第 4 节 |
| 已有 PyMO 游戏（`.py` 脚本或已解密的 `#命令` 文本） | `cpymo-tool convert` + 直接跑或先转 ykm | 第 9 节 |

三条路最后产出的都是同一个东西：一个含 `gameconfig.txt` 和 `script/` 的目录。CPyMO 只认这个目录，不关心你用哪种语法写的脚本。

---

## 1. 一个游戏目录长什么样

**引擎**读取的是 `<游戏目录>/gameconfig.txt` 与 `<游戏目录>/script/<名字>.txt`。目录里其余子目录按约定命名：

```
MyGame/
├── gameconfig.txt          必须
├── icon.png                可选，57×57
├── bg/                     背景图
├── bgm/                    背景音乐
├── chara/                  立绘及其遮罩
├── save/                   存档（引擎运行时写入，不需要预置）
├── script/                 脚本
├── se/                     音效
├── system/                 界面图片与字体
├── video/                  影片（需要后端支持）
└── voice/                  语音
```

只有 `gameconfig.txt` 和 `script/` 是硬性要求，其余目录按你的游戏是否需要来建。缺目录不会报错，只是对应命令没有资源可用。

### 1.1 pak 打包（可选）

**引擎**启动时按固定名字打开四个包（`cpymo_assetloader.c:30/40/50/60`）：

* `bg/bg.pak`、`chara/chara.pak`、`se/se.pak`、`voice/voice.pak`

打不开就当没有。**但某个包一旦存在，这个目录就只从包里读，不再看散文件，也不会回落**（`load_image_pixels` 是 `if (using_pkg) 读包 else 读文件` 的二选一）。所以「大文件打包、零碎的散着放」是不行的：要么整个目录一起打包，要么整个目录都不打。

没有 `system.pak` 和 `bgm.pak`。所以 `system/`、`bgm/`、`script/`、`video/` 里的文件必须以散文件形式存在。

打包用 `cpymo-tool pack`（见 8.1）。打不打 pak 对游戏逻辑没有影响，只影响文件数量和读取开销，小游戏不必打。

`convert` 和 `strip` 走的是同一套资源遍历（`cpymo_tool_asset_filter.c`）：**输入目录里有包就写回包，没有就写散文件**，加 `--pack` 强制输出成包（`cpymo-tool/main.c:76-78`）。所以转换一个带 `bg.pak` 的游戏不会在磁盘上冒出一堆散图。

### 1.2 文件命名

资源名在脚本里不带扩展名，扩展名由 `gameconfig.txt` 的 `bgformat` / `charaformat` 等键决定。所以：

```
bg/BG001.jpg      ←→  脚本里写 BG001
chara/A.png       ←→  脚本里写 A
```

改名时只改前半段，不要顺手改扩展名，否则 `gameconfig.txt` 也得跟着改。

---

## 2. gameconfig.txt

一行一个键值，**逗号分隔**，`key,value`。行首尾空白会被削减。`#` 不是注释符，引擎不认注释（**引擎**：`cpymo_gameconfig.c` 的 `cpymo_dispatch_gameconfig` 只按逗号切，没有注释分支）。

### 2.1 最小可用集合

一个新游戏真正需要写的只有这几行：

```
gametitle,我的第一个游戏
engineversion,1.2
startscript,start
scripttype,pym
platform,pygame
imagesize,1280,720
bgformat,jpg
charaformat,png
charamaskformat,png
bgmformat,mp3
seformat,mp3
voiceformat,mp3
```

`startscript` 是启动时进入的脚本名，对应 `script/<名字>.txt`。**这一项必须写**：引擎不会替你补默认值，空着会去找 `script/.txt` 然后启动失败（**引擎**：`cpymo_script.c` 用 `script/%s.txt` 拼路径，而 `cpymo_gameconfig.c` 的默认值段没有填 `startscript`）。

`imagesize` 是你的**逻辑分辨率**，所有坐标、百分比、立绘缩放都以它为基准。它不必等于窗口大小：窗口会把逻辑画面等比缩放到窗口里。做横屏 AVG 常用 `1280,720` 或 `800,600`；老 PyMO 素材是 `320,240`（S60v3）或 `540,360`（S60v5）。

### 2.2 完整键表（以引擎为准）

| 键 | 引擎默认值 | 含义 |
| --- | --- | --- |
| `gametitle` | 空 | 标题，最长 255 字节。可以含 `\n` 换行 |
| `startscript` | 空（必须自己写） | 启动脚本名，最长 63 字节 |
| `scripttype` | 空 | 固定写 `pym`（三个字母）。写错不会崩，但启动时会弹一条「非 PyMO 脚本」警告（**引擎**：`cpymo_engine.c` 只比较前四个字符是否为 `p y m \0`） |
| `platform` | `pygame` | 平台标记，只影响两件事：① 等于 `s60v3` / `s60v5` 时启用塞班那套 `_mask` 遮罩（见 `charamaskformat`）；② 等于 `cpymohd1` / `cpymohd2` / `cpymohd3` 时把 feature level 设成 1 / 2 / 3（`cpymo_engine.c:116-124`），也就是**用这一行给游戏开 Level 1 的 Lua 能力**。其它值一律 Level 0 |
| `engineversion` | 空 | 形如 `1.2`，按 `.` 拆成主次版本 |
| `imagesize` | `0,0` | 逻辑分辨率，`宽,高` |
| `bgformat` | 空 | 背景扩展名，`jpg` 或 `.jpg` 都行 |
| `charaformat` | 空 | 立绘扩展名，同上 |
| `charamaskformat` | 空 | 立绘遮罩扩展名。**只有 `platform` 写成 `s60v3` / `s60v5` 时才会去加载 `<名字>_mask.<这个扩展名>`**，其它平台一律忽略（**引擎**：`cpymo_assetloader.c` 的 `cpymo_assetloader_load_image_with_mask` 先判 `cpymo_gameconfig_is_symbian`）。遮罩缺失不致命，会退回不带遮罩的普通图 |
| `bgmformat` | `mp3` | 背景音乐扩展名 |
| `seformat` | `mp3` | 音效扩展名 |
| `voiceformat` | `mp3` | 语音扩展名 |
| `cgprefix` | `EV_` | CG 文件名前缀，决定哪些图进鉴赏 |
| `fontsize` | 4 | 字号，被夹到 4–1024；实际几乎总被 `save/config.csav` 覆盖 |
| `textcolor` | 白 | 正文文字颜色，**必须写 `#RRGGBB`**，写 `white` 会变品红 |
| `msgtb` | `6,0` | 文本框上下留白，**一个键两个数**：`上,下`（字段名是 `msgtb_t`/`msgtb_b`，但配置文件里只有 `msgtb`） |
| `msglr` | `10,7` | 文本框左右留白，`左,右`（字段 `msglr_l`/`msglr_r`） |
| `nameboxorig` | `0,0` | 名字框偏移，`x,y`，各夹到 ±1024（字段 `nameboxorg_x`/`nameboxorg_y`） |
| `textspeed` | `3` | 打字速度，3 位（0–7） |
| `bgmvolume` | `0` | 初始 BGM 音量，4 位（0–15） |
| `vovolume` | `0` | 初始语音音量，4 位（0–15） |
| `hint` | `1` | 是否显示操作提示 |
| `grayselected` | `1` | 选中项是否灰化 |
| `playvideo` | `1` | 是否允许播影片 |
| `namealign` | `0` | 名字对齐：`l`/`L`→1 左，`m`/`M`→0 中，`r`/`R`→2 右 |

`bgformat` 这类扩展名键允许带点也允许不带（**引擎**：`SETUP_EXT` 宏两种都吃）。

`msgtb`、`msglr`、`nameboxorig` 这三个键要在**同一行**用逗号给两个数，比如 `msgtb=6,0`。写成 `msgtb_t=6` 不会被识别：引擎按整键名精确比对，认不出的键**静默丢掉**（`cpymo_dispatch_gameconfig` 是一条 if-else 链，没有兜底分支，也不打警告），值仍是默认值。所以「改了配置没反应」第一个要查的就是键名拼写。

`platform` 值会被削减空白，`imagesize` 用逗号分两段。

### 2.3 手册里有、CPyMO 里没有的键

老手册第 4 章列的下面这几个键 **CPyMO 不读**，写了也无害，但没有任何效果：

* `font`：CPyMO 不用数字选字体，字体是后端加载的字体文件（见 7.3）。
* `fontaa`：抗锯齿是后端行为，不由游戏配置控制。
* `prefetching`：CPyMO 的 `bgm` 预取由 `music_list` 机制另行处理。

反过来，CPyMO 有而手册没有的键：`cgprefix`、`msgtb`、`msglr`、`nameboxorig`、`textcolor`、`vovolume`。

### 2.4 存档会覆盖一部分配置

`save/config.csav` 里存了玩家在设置界面改过的字号、音量、打字速度等。**玩家一旦进过设置界面，改 `gameconfig.txt` 就不再影响他的体验。** 调试时如果发现 `fontsize` 改了没反应，先删掉 `save/config.csav`。

这条经常在测试里踩到：**跑测试游戏前先确认 `save/config.csav` 不存在**。

---

## 3. 坐标系、颜色、时间

### 3.1 百分比坐标

绝大多数位置参数是**百分比**（0–100），相对于 `imagesize`。

* `50,50` 是画面正中。
* `3,67,50,100` 是左起 3%、上起 67% 到左起 50%、上起 100% 的矩形（文本框常用）。
* 少数命令接受绝对像素（例如 `text` 的四个角）。

具体每个参数用哪种，看 [CPyMO 指令参考.md](CPyMO%20指令参考.md) 里对应命令那一行。

### 3.2 颜色

颜色一律写 `#RRGGBB`（六位十六进制），**不认颜色名**。写 `white`、`red` 得到的是品红，不是白色。

**引擎**：`cpymo_str_as_color` 先看首字符是不是 `#`，不是就直接返回 `cpymo_color_error`（品红 241,75,233）；然后只检查 `#` 后面是否全是十六进制字符，从**末位往前**依次填 `b`、`g`、`r`。三位简写（`#FFF`）不报错，但按这个方向填出来是 `(15,255,255)` 而不是白色，**别用三位简写**。

### 3.3 时间

所有 `time` 参数单位是毫秒。`300` 是 0.3 秒，写 `0` 是瞬间完成。

### 3.4 立绘坐标模式（cm0–cm6）

立绘的定位命令（`chara_pos`、`chara_y`、`chara_scroll`、各 `*_multi` 收集行等）最后一个参数是坐标模式，取值 0–6。**在 PyMO 文本脚本里必须写纯数字**（写 `5`），不要写 `cm5`：引擎用 `atoi` 取这个参数（**引擎**：`cpymo_str.c` 的 `cpymo_str_atoi`），`atoi("cm5")` 得到 `0`，立绘会莫名其妙跑到左上角。`cm5` 这种写法属于 YukimiScript 侧（`libpymo.ykm` 把它声明成类型，`macro coord_mode=cm5`），由 `ykmc` 编译成数字。**引擎** `cpymo_charas.c` 的语义是：

| 模式 | 水平 | 垂直 |
| --- | --- | --- |
| cm0 | 立绘左沿到屏幕左沿 | 立绘上沿到屏幕上沿 |
| cm1 | 立绘中点 | 上沿 |
| cm2 | 立绘右沿到屏幕右沿 | 上沿 |
| cm3 | 立绘中点 | 立绘中点 |
| cm4 | 左沿 | 立绘下沿到屏幕下沿 |
| cm5 | 中点 | 下沿 |
| cm6 | 右沿 | 下沿 |

省略这个参数时按 5 处理：水平居中、脚踩屏幕底边。单条 `#chara` 命令**不接受**这个参数，永远按 5 摆。

`cm7` 以上引擎直接返回 `CPYMO_ERR_INVALID_ARG`（**引擎**：`cpymo_chara_convert_to_mode0_pos` 里判 `coord_mode > 6`）。手册里如果有更高编号，那是 PyMO 的别的约定，不要照抄。

---

## 4. 十五分钟写出第一个游戏

### 4.1 准备目录

```bash
mkdir -p MyGame/{script,bg,chara,system,bgm,se,voice,save}
```

放一张背景到 `bg/BG001.jpg`，一张立绘到 `chara/A.png`。没有素材也能先跑：把下面脚本里 `bg` / `chara` 那两行注释掉（实际上引擎不认注释，删掉即可）。

### 4.2 写 gameconfig.txt

```
gametitle,我的第一个游戏
engineversion,1.2
startscript,start
scripttype,pym
platform,pygame
imagesize,1280,720
bgformat,jpg
charaformat,png
charamaskformat,png
bgmformat,mp3
seformat,mp3
voiceformat,mp3
fontsize,24
```

### 4.3 写 script/start.txt

```
#label start
#bg BG001,BG_NOFADE
#chara 1,A,50,0,300
#bgm BGM001
#say 旁白,这是一个 CPyMO 游戏。
#say A,你好，我是 A。
#se SE001
#say A,刚才的按键有声音吗？
#bgm_stop
#change start
```

存成 **UTF-8**（无 BOM 最保险，有 BOM 时脚本名/第一行可能带上脏字符），换行 CRLF / LF 都行（**引擎**：载入时会做 CR 替换）。

`#say` 取两个参数：`名字,正文`。**只写一个参数（没有逗号）时整个串当正文**，名字框为空（**引擎**：`cpymo_interpreter.c` 的 `D("say")` 分支，`POP_ARG(name_or_text)` 之后若第二参为空则对调）。

`#chara` 的参数顺序是 **`编号,资源名,x 百分比,层`**，不是「名字、位置、坐标模式」（**引擎**：`cpymo_interpreter.c` 的 `D("chara")` 分支里依次弹 `chara_id_or_time / filename / pos_x / layer` 四段）。编号是整数，同一个编号再写一次就替换掉那张立绘；层号越大越靠前；坐标模式在这里**固定为 5**（水平居中、脚踩底边），想让立绘站别的位置用 `#chara_pos 编号,x,y,cm编号`。

`#chara` 是**一行一组**的收集命令：`编号,资源名,x 百分比,层` 四个逗号格一组，第 5 格起再读的那一个数字是**淡入时长（毫秒）**，读完这一行就结束。所以：

* `#chara 1,A,50,0` —— 摆一张，瞬现（时长 0）。
* `#chara 1,A,50,0,300` —— 摆一张，300 ms 淡入。
* `#chara 1,A,50,0,2,B,30,1,300` —— 一次摆两张，都 300 ms 淡入（四格一组连写，最后一格给时长）。
* `#chara 300` —— 这种只有数字的独立行**什么都不做**（解析出时长，可是没有立绘可摆）。老教程里「再补一行数字收尾」的说法在 CPyMO 里不成立，别照抄。

**引擎**：`cpymo_interpreter.c` 的 `D("chara")` 中 `POP_ARG` 只从当前行取格（`cpymo_parser_curline_pop_commacell` 不跨行），当后三格全空时它把首格当毫秒数然后 `break`。

`chara_multi`、`chara_pos`、`chara_y`、`chara_cls` 这些 `*_multi` 族同理：PyMO 文本里是「一行一组 + 末格时长」，只有 YukimiScript 侧才拆成「收集行 + `*_do` 收尾行」两条命令（参数见 [CPyMO 指令参考.md](CPyMO%20指令参考.md)）。

`#bg` 的参数顺序是 **`资源名,转场,时间,x,y`**（**引擎**：`D("bg")` 分支）。转场省略时默认 `BG_ALPHA`；时间可以写毫秒数，也可以写 `BG_VERYFAST`（100ms）、`BG_FAST`（175ms）、`BG_NORMAL`（250ms）、`BG_SLOW`（500ms）、`BG_VERYSLOW`（1000ms），省略时 300ms。`x`/`y` 是像素偏移，省略为 0。

`BG_NOFADE` 是要写在**转场位**上的符号，不是「不要转场」的时间开关；想要瞬切就连时间一起省，让它走默认 300ms 淡入，或者写 `BG_NOFADE` + `BG_VERYFAST` 把时长压到 0.1 秒。

`#change start` 的意思不是「跳转到 label start」，而是**重新载入脚本 `start`**。在这里它的作用是让对话结束后回到开头。真正的同脚本跳转用 `#goto 标签名`。

### 4.4 跑起来

桌面平台（SDL2 后端）：

```bash
cd cpymo-backends/sdl2
# 或按 README 用 CMake / nmake 构建
./cpymo /path/to/MyGame
```

没有图形环境时可以用 ascii-art 后端做纯逻辑验证：

```bash
cd cpymo-backends/ascii-art
make -j2
./cpymo-ascii-art /path/to/MyGame
```

按键：`W` `A` `S` `D` 移动选择，空格或 `J` 确认，`K` 取消，`L` 快进。

### 4.5 逐条验证清单

一个「能跑」不等于「对」。新写完一段脚本，至少确认：

1. 标题栏/标题画面显示的是 `gametitle`，没有乱码 —— 乱码是字体问题（见 7.3）。
2. 背景铺满整个逻辑画面，没有被拉伸变形 —— 素材比例和 `imagesize` 不一致时会被拉伸。
3. 立绘站在画面里而不是半个身子在屏幕外 —— 位置和 `cm` 模式是否配错。
4. `#se` 有声音但不会盖住 BGM。
5. 对话结束后 `#change start` 能回到开头，存档再读档能回到同一句。

---

### 4.6 跳转、子脚本与随机数

四条「换地方继续执行」的命令，语义差别很大，混用是最常见的 bug 来源：

| 命令 | 作用范围 | 会回来吗 |
| --- | --- | --- |
| `#goto 标签` | 只在**当前脚本文件**内找标签 | — |
| `#call 脚本名` | 切到 `script/脚本名.txt`，从头开始跑 | `#ret` 或子脚本跑完时回到 `#call` 的下一行 |
| `#change 脚本名` | 用另一个脚本**替换**当前脚本 | 不回当前脚本；但调用链保留，新脚本里 `#ret` 仍回到最初的调用者 |
| `#ret` | — | 回调用者；在顶层脚本里等于结束游戏 |

**引擎**：`#goto` 走 `cpymo_interpreter_goto_label`，只在当前脚本里查标签；`#call` 新建一个 interpreter 并把当前这个记为它的 `caller`；`#change` 换掉当前 interpreter 的 script，把原来的 `caller` 原样传给新脚本；`#ret` 在 `caller == NULL` 时返回 `CPYMO_ERR_NO_MORE_CONTENT`，也就是「没内容了」。

要点：

* `#call` 的参数是**脚本文件名**（`script/` 下，不带 `.txt`），不是标签。想把共用桥段抽出来，就单独建 `script/common.txt`，然后 `#call common`。
* `#goto` 跳不到别的脚本，跨脚本只能用 `#call` / `#change`。
* `#change` 看着像「换剧本」，实际是「当前脚本演完了，接着演另一个」。因为 `caller` 被保留，所以从子脚本里 `#change` 之后仍然能 `#ret` 回去——这就是它和「先 `#ret` 再 `#call`」的区别。

**条件分支**

`#if 条件,子命令` —— **第一个逗号之前是完整条件**，逗号之后是子命令名，子命令自己的参数照常用逗号跟在后面：

```
#if FSEL=0,goto NEW_GAME
#if S02>2,bg BG004,BG_FAST
```

* 运算符只有 `=`、`!=`、`<>`、`>`、`>=`、`<`、`<=`（**引擎**：`D("if")` 里逐个 `cpymo_str_equals_str` 比；写成别的样子会打 `[Error] Bad if expression "..." in script %s(%u).`）。
* 两侧可以是常量，也可以是变量名。**右侧写了不存在的变量时整行被静默跳过**（`cpymo_vars_access` 拿到 NULL 就直接 `CONT_NEXTLINE`），不报错——「条件好像没生效」多半是这个。
* 子命令只取逗号之后的第一个词，所以能内联的只有单条命令；要连着做好几件事就 `#if 条件,goto 标签`，把活儿放在标签下面。

**随机数**

`#rand 变量,最小值,最大值` 是**闭区间**：`#rand S,1,3` 的结果可能是 1、2、3。

**引擎**：`D("rand")` 先比 `max` 和 `min`，`max - min <= 0` 时直接报 `[Error] In script %s(%u), max value must bigger than min value for rand command.` 并返回 `CPYMO_ERR_INVALID_ARG`——**上下界不能相等**，想要固定值就别用 `#rand`。

## 5. 三个最常写的范例

### 5.1 标题画面 + 选项

```
#label START
#bg title,BG_NOFADE,BG_VERYFAST
#select_text 4,开始,继续,鉴赏,结束,3,67,50,100,#3E8BF4,1
#if FSEL=0,goto NEW_GAME
#if FSEL=1,goto LOAD_GAME
#if FSEL=2,goto ALBUM
#if FSEL=3,goto QUIT
```

**引擎**：`#select_text` 一条命令吃下所有参数，顺序是

```
数量,选项1..选项N,x1,y1,x2,y2,颜色,初始选中序号
```

返回值写进变量 **`FSEL`**，从 0 开始（**引擎**：`cpymo_select_img.c` 里 `cpymo_vars_set(..., "FSEL", sel)`）。所以第一个选项对应 `FSEL=0`。

四个坐标 `x1,y1,x2,y2` 是**百分比**：`x1,y1` 是文本框左上、`x2,y2` 是右下。横向把每行文字在 `[x1,x2]` 里居中，纵向把整块文字在 `[y1,y2]` 里居中（**引擎**：`cpymo_select_img.c` 的 `configuare_end_select_text`，`box_w = x2 - x1`、每行 `x = (box_w - w)/2 + x1`，最后统一加 `(y2 - y)/2`）。示例里的 `3,67,50,100` 读作「下半屏、左半边那块居中区域」。

颜色位照 3.2 写 `#RRGGBB`，写颜色名会得到品红。

手册里的这个例子和引擎行为一致，可以直接用。

### 5.2 多行选择支

```
#label START
#bg title
#sel 3
去学校
去海边
回家睡觉
#if FSEL=0,goto SCHOOL
#if FSEL=1,goto SEA
#if FSEL=2,goto SLEEP
```

`#sel N` 后面**紧跟 N 行**纯文本，每行一个选项。这是 PyMO 原版的写法，CPyMO 保留了（**引擎**：`D("sel")` 分支，注意它会把 `#sel 0` 这种旧写法排除掉）。

`#sel` 与 `#select_text` 的区别：`#sel` 用系统图片当选项底（`system/option.png`、`system/sel_highlight.png`），`#select_text` 用纯文字画选项。做新游戏建议 `#select_text`，样式更容易统一。

### 5.3 全局变量：每次进游戏随机一张背景

**`S` 开头的变量是全局变量**，存 `global.csav`，读档不影响它（**引擎**：全局变量进 `e->vars.globals`，与存档分开写）。这正好用来记「本局已经随机过没有」：

```
#label START
#if S02=0,goto RAND_BG
#if S02=1,goto BG2
#if S02=2,goto BG3
#if S02=3,goto BG4

#label RAND_BG
#rand S02,1,3
#if S02=1,goto BG2
#if S02=2,goto BG3
#goto BG4

#label BG2
#bg BG002
#goto TITLE
#label BG3
#bg BG003
#goto TITLE
#label BG4
#bg BG004
#goto TITLE

#label TITLE
#select_text 2,开始,结束,3,67,50,100,#FFFFFF,0
#if FSEL=0,goto NEW_GAME
#change start
```

要重置随机结果，在「结束」分支里写 `#set S02,0`。

注意 `#rand` 的上下界是**闭区间**：`#rand S02,1,3` 可能得到 1、2、3。

---

## 6. 鉴赏系统（CG 与音乐）

鉴赏系统由两套东西组成：一套是你的资源，一套是「列表脚本」。

### 6.1 哪些图会进 CG 鉴赏

**引擎**用 `cgprefix`（默认 `EV_`）判断：`bg` 与 `chara` 命令加载的资源名若以这个前缀开头，就会被标记为「可解锁 CG」（**引擎**：`cpymo_album_cg_unlock`，调用点在 `cpymo_interpreter.c` 的 `bg` 与 `chara` 分支）。所以：

* 普通背景叫 `BG001`，永远不进鉴赏。
* 想进鉴赏的图命名成 `EV_001`，并在 `gameconfig.txt` 里保留 `cgprefix,EV_`（或改成你自己的前缀）。

解锁的时机就是「玩家真的看到它」。没看过的 CG 在鉴赏里显示占位图 `system/cvThumb.png`。

### 6.2 album_list.txt

在 `script/` 下建 `album_list.txt`（**它是脚本目录里的资源脚本，不是纯文本清单**），一行一页：

```
页码,缩略图数,页名,文件名1,文件名2,...
```

* **引擎**只取 `页码 == 当前页 + 1` 的行（**引擎**：`cpymo_album.c` 的列表解析），所以页码从 1 开始。
* 中间那个「页名」字段被引擎丢弃，随便写。
* 每页最多 25 张（网格 5 列 × 5 行）。
* 行尾追加一个 `,1` 表示这一页不需要解锁、直接显示。

例子（第 1 页 3 张，第 2 页 2 张）：

```
1,3,第一页,EV_001,EV_002,EV_003
2,2,第二页,EV_004,EV_005
```

缩略图网格位置由引擎算：5 列，缩略图宽高为逻辑画面的 0.17 倍，左上角在 `(0.03 + 0.19 × 列, 0.02 + 0.19 × 行)`（**引擎**：`cpymo_album.c`）。你不需要自己排位置，但需要准备 5 张背景：`system/albumbg_0.png`、`albumbg_1.png`……从 0 编号。

### 6.3 进入鉴赏

```
#album
```

不带参数时用 `album_list` 作为列表脚本、用 `albumbg` 作为背景图名前缀（**引擎**：`cpymo_interpreter.c` 的 `D("album")`，空参时 `list_name="album_list"`、`ui_name="albumbg"`）。带参数时参数就是列表名：

```
#album album2
```

此时背景图前缀也跟着变成 `album2`，也就是说你需要 `system/album2_0.png` 这类文件。这是 PyMO 0.9 起才有的能力。

### 6.4 音乐鉴赏

```
#music
```

`#music` **不接受参数**（**引擎**：直接调 `cpymo_music_box_enter`），列表固定读 `music_list`。在 `script/` 下建 `music_list.txt`，一行一首、两个逗号格：

```
BGM001,片头曲
BGM002,夏日回忆
```

第一格是 `bgm/` 下的文件名（不带扩展名），第二格是列表上显示的文字。

**音乐鉴赏没有解锁判定**（**引擎**：`cpymo_music_box.c` 解析时完全不读任何 flag，`grep unlock` 在该文件里零命中），表里写几首就显示几首。这一点和 CG 鉴赏不同——只有 album 才有「看过才亮、否则显示 `cvThumb.png`」。

### 6.5 缩略图缓存

第一次进鉴赏要现场缩放所有 CG，会明显卡一下。用工具预生成缓存：

```bash
cpymo-tool gen-album-cache /path/to/MyGame
```

它在游戏目录里生成缓存文件，之后再进鉴赏就不卡了。

---

## 7. 素材、字体与后端

### 7.1 推荐的素材规格（手册给的）

老手册给的参数偏保守，但作为「什么格式最保险」的参考仍然有效：

| 素材 | 格式 | 建议 |
| --- | --- | --- |
| 背景 | JPG | 640×480 或与你 `imagesize` 同比例 |
| 立绘 | PNG（256 索引色或 RGBA） | 透明区域用 alpha，不要用纯色抠图 |
| 立绘遮罩 | PNG 灰度图 | 与立绘同尺寸；**只有 `platform` 写成 `s60v3` / `s60v5` 时**才按 `charamaskformat` 去找 `_mask` 图 |
| BGM | MP3 44100 Hz / 192 Kbps / 立体声 | — |
| SE、语音 | MP3 或 WAV 22050 Hz / 48 Kbps / 单声道 | 语音一定单声道 |

手册里 S60v3 用的 320×240、S60v5 用的 540×360，那是诺基亚时代的屏。做现代平台直接用 1280×720 起。

**遮罩的语义**（塞班时代遗物）：`A.png` 的遮罩是 `A_mask.png`，同尺寸的 256 级灰度图，**越黑越透明、越白越不透明**。Android 与 PC 版不用遮罩，直接用带 alpha 的 png。老手册提到的 `pymo_mask_to_png` 工具 CPyMO 仓库里没有，移植老素材要自己把「图 + 灰度遮罩」合成成 RGBA png（ImageMagick 里 `compose CopyOpacity` 一类操作即可）。

### 7.2 系统图片清单

`system/` 下这些名字是引擎**直接按名字找**的（**引擎**：`cpymo_assetloader_load_system_image` 的各调用点）：

| 文件 | 谁在用 |
| --- | --- |
| `icon.png` | 图标，57×57（游戏选择器与打包用） |
| `menu.png` | 菜单背景（`cpymo_rmenu.c`） |
| `option.png` | `#sel` / `select_img` 的选项底图（`cpymo_select_img.c`） |
| `sel_highlight.png` | 选中高亮条（同上） |
| `message_cursor.png` | 打字光标，约 24×24（`cpymo_say.c`） |
| `albumbg_N.png` | 鉴赏每页背景，从 0 编号（`cpymo_album.c`） |
| `cvThumb.png` | 未解锁 CG 的占位缩略图 |

对话框底图（老游戏里叫 `message.png`）和名字框底图（`name.png`）**不是固定名字**：它们由 `#textbox <对话框图>,<名字框图>` 命令按名字指定，你想叫什么都可以。压根不调 `#textbox` 时，引擎用内置的画法，一个图都不需要（**引擎**：`cpymo_interpreter.c` 的 `D("textbox")` 把两个名字直接传给 `cpymo_say_load_msgbox_and_namebox_image`）。

这些底图配一张同名 `_mask` 是老 PyMO 的约定，但 **CPyMO 只在 `platform` 写成 `s60v3` / `s60v5` 时才会加载 `_mask`**（与 2.2 的 `charamaskformat` 同一个门控），其它平台一律忽略。新游戏直接用带透明通道的 png 就行。

只做新游戏时，这些图都可以先用纯色块顶替，跑通流程再换美术。

### 7.3 字体

字体不是 `gameconfig.txt` 管的，是**后端**在启动时加载的。SDL2 后端的查找顺序（**引擎**：`cpymo_backends/sdl2/cpymo_backend_font.c`）：

1. `<游戏目录>/system/default.ttf`
2. `<游戏目录>/system/default.otf`
3. 编译期宏 `GAME_SELECTOR_DIR` 指向的目录（游戏选择器用；不定义就没有这一步）
4. 各平台回退（Windows 会去系统字体目录，Android 去 `/sdcard/pymogames/default.ttf`，iOS 去文档目录）

所以**把字体放进游戏目录的 `system/default.ttf`** 是最省事的做法，游戏跟着目录走，换机器不会乱码。

其他平台的约定位（README 各平台章节）：

* 3DS：`sdmc:/pymogames/font.bcfnt` 优先，找不到用内置字体。
* Switch：`/pymogames/default.ttf`；乱码就是它没放。
* PSP：`PSP/GAME/CPYMO/default.ttf`，建议小于 2MB。
* iOS：应用文档目录下 `default.ttf`。

中文字体动辄十几 MB。要控制体积，先用 `cpymo-tool strip` 裁掉不用的资源，或换一个子集字体。

### 7.4 后端差异会咬人的地方

| 事情 | 哪些后端支持 |
| --- | --- |
| 影片（`movie`） | 需要 FFmpeg；只链 SDL2_mixer 时没有视频 |
| 文本输入（`input`） | 只有实现了 SDL_TEXTINPUT 的后端 |
| 立绘遮罩（`_mask`） | 与后端无关，只跟 `platform` 有关（见 7.2） |
| 语音/音效 | 需要有音频后端，ascii-art 后端没有 |

在 ascii-art 后端上测游戏时，凡是涉及画面和声音的命令都只能验证「不崩、不报错」，**不能验证效果**。这是环境限制，不是 bug。

### 7.5 怎么把游戏跑起来

所有后端都只接受「游戏目录」这一个位置参数，不给就当作当前目录：

```bash
cpymo-sdl2      /path/to/MyGame   # SDL2 桌面版
cpymo-ascii-art /path/to/MyGame   # 纯文本后端
```

ascii-art 后端在 `cpymo-backends/ascii-art/`，`make LUA=1` 就能产出 `cpymo-ascii-art`。它把画面画成字符网格，所以只能验脚本流程、变量、分支和「不崩」，验不了美术效果（原因见 7.4）。按键：`W/S/A/D` 移动，空格或 `J` 确认，`K` 取消，`L` 跳过。

平台版的游戏目录位置由各平台自己约定，多半在存储卡的固定位置，构建/部署细节见 README 的对应章节：

- 3DS / Switch：`sdmc:/pymogames/` 下的游戏目录（3DS 还有路径长度限制，见 README）
- PSP：`PSP/GAME/CPYMO/`，并且资源必须先过一遍 `cpymo-tool convert psp`
- Android：`/sdcard/pymogames/`
- iOS：iTunes 文件共享的文档目录

---

## 8. 打包与发布：cpymo-tool

`cpymo-tool` 是仓库自带的资源工具，所有子命令都在 `cpymo-tool/main.c` 的帮助文本里。构建后直接跑 `cpymo-tool` 会打印用法。

### 8.1 子命令一览

| 子命令 | 用法 |
| --- | --- |
| `unpack` | `unpack <pak> <扩展名带点> <输出目录>`，拆包 |
| `pack` | `pack <输出.pak> <文件...>` 或 `pack <输出.pak> --file-list <list.txt>`，打包 |
| `resize-image` | `resize-image <源> <目标> <宽> <高> [--load-mask [格式]] [--create-mask [格式]] [--out-format png/bmp/jpg]` |
| `pack-spritesheet` | `pack-spritesheet <输出> <列数> <文件...> [--load-mask] [--create-mask] [--out-format]` |
| `gen-album-cache` | `gen-album-cache <游戏目录>`，生成鉴赏缩略图缓存 |
| `strip` | `strip <游戏目录> <输出目录> [--pack]`，剔除未引用的资源 |
| `convert` | `convert <目标平台> <游戏目录> <输出目录> [--pack]`，一把梭：缩放图片、改配置、可选打包 |

`strip` 很有用：它扫脚本找出真正引用到的资源名，把没用到的文件删掉。发布前跑一次常常能省一半体积。**但要先备份**，脚本里用变量拼出来的资源名它可能认不出来。

不带 `--pack` 时，这两个命令保持输入的形态：源目录散着就给散文件，源目录有 pak 就写回 pak。

### 8.2 convert 的目标平台规格

`convert` 按内置表改写所有图片尺寸并调整 `gameconfig.txt`（**引擎**：`cpymo_tool_convert.c` 的规格表）：

| 目标 | 分辨率 | 生成遮罩 | 影片 | 等比适配 | 强制缩放 | 图片格式（bg/立绘/遮罩） | 写入的 platform |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `s60v3` | 320×240 | 是 | 是 | 否 | 是 | jpg / jpg / jpg | `s60v3` |
| `s60v5` | 540×360 | 是 | 是 | 否 | 是 | jpg / jpg / jpg | `s60v5` |
| `3ds` | 400×240 | 否 | 是 | 是 | 否 | jpg / png / 不生成 | `pygame` |
| `pymo` | 800×600 | 否 | 是 | 否 | 否 | jpg / png / 不生成 | `pygame` |
| `psp` | 480×272 | 否 | 否 | 否 | 是 | jpg / png / 不生成 | `pygame` |
| `wii` | 640×480 | 否 | 否 | 否 | 是 | jpg / png / 不生成 | `pygame` |

几点值得注意：

* `3ds` 会**等比适配**（加黑边），`psp` / `wii` / `s60v3` / `s60v5` 是**强制拉伸**。竖构图的立绘在强制拉伸后会变形，移植前先决定要不要自己重做素材。
* 只有 `s60v3` / `s60v5` 会**生成立绘遮罩**。其他目标保持你原来的遮罩设置。
* 除了 `s60v3` / `s60v5`，其他目标把 `platform` 写成 `pygame`。`platform` 在 CPyMO 里只有两个用途：塞班遮罩门控，以及 `cpymohd1/2/3` 这三个值用来选 feature level（见 2.2）。**注意 `convert` 会把输出目录的 `platform` 覆盖成上表的值**，所以转换完再手工把 `platform` 改成 `cpymohd1` 之类，否则 Level 1 的脚本跑不起来。
* PSP 的游戏数据包必须用 `convert psp` 转换过才能跑（README 的 PSP 章节明确要求）。

音频格式不在上表里，但转换时会按目标的可用解码能力筛：`s60v3` / `s60v5` / `3ds` / `psp` 这四个接受 OGG/MP3/WAV，`pymo` 与 `wii` 只接受 OGG/WAV。老手册说 pymo 支持 `wav`/`mp3`/`amr`/`aac`/`midi`。`amr` 与 `aac` 任何 CPyMO 后端都解不了（SDL_mixer 没有这两个解码器）。`midi` 要看构建：SDL2 后端确实请求了 `MIX_INIT_MID`（`cpymo_backend_audio_sdl2_mixer.c:63-68`），但真能出声取决于 SDL_mixer 编译时带没带 timidity / fluidsynth，不能指望。移植别人的游戏时统一先转成 OGG 或 MP3 最省事。

### 8.3 一个完整的发布流程

```bash
# 1. 剔掉没引用的资源
cpymo-tool strip MyGame MyGame-stripped

# 2. 按目标平台转换
cpymo-tool convert 3ds MyGame-stripped MyGame-3ds --pack

# 3. 生成鉴赏缓存（如果用了鉴赏）
cpymo-tool gen-album-cache MyGame-3ds

# 4. 把 MyGame-3ds 整个目录拷到目标设备的约定位置
```

`--pack` 会把 `bg`/`chara`/`se`/`voice` 打成 pak，其余目录仍是散文件。

---

## 9. 移植一份老 PyMO 游戏

### 9.1 先看清你要移植的是什么

PyMO 游戏通常是两种形态之一：

1. **`.py` 脚本 + 素材**：脚本是 Python 代码，直接调用 PyMO 的 Python API。
2. **已解密的 `#命令` 文本脚本 + 素材**：已经被人从 `.py` 或打包文件里还原成文本。

形态 1 需要先把 Python 代码翻译成文本脚本（或翻译成 YukimiScript）。这一步没有通用工具，只能人工/半自动做：把 `py` 里的调用逐句替换成等价命令。手册第 2 章给的建议仍然正确：

> 批量处理、用正则或 Python 写脚本做替换，不要一句一句手改。

形态 2 可以直接跑，见 9.2。

### 9.2 直接跑：先修四件事

把老游戏的目录结构对齐 CPyMO 的要求，通常只需要改这四处：

1. **`gameconfig.txt`**：老游戏可能缺 `scripttype`（补 `pym`）、缺 `imagesize`（补上原始分辨率）、`startscript` 名要和 `script/` 里的文件名对上。
2. **扩展名**：老游戏常见 `bgformat,.jpg`（带点）。CPyMO 两种都吃，不用改。
3. **编码**：脚本必须是 UTF-8。老素材若是 GBK/Shift-JIS，先转码再放进去，否则满屏乱码。
4. **文件名大小写**：资源名在 Linux/Android 上区分大小写，Windows 上不区分。老游戏在 Windows 上写的脚本，搬到手机常常报「找不到资源」就是这个原因。用 `strip` 或自己写脚本核对一遍引用名和实际文件名。

### 9.3 跑起来之后逐条对差

老脚本能跑不等于行为一致。按这个顺序检查，越靠前的问题越常见：

| 症状 | 大概原因 | 怎么办 |
| --- | --- | --- |
| 立绘位置全错 | 坐标模式写错 | 见 3.4：PyMO 文本里必须写纯数字 `0`–`6`，写 `cm5` 会被 `atoi` 读成 0（跑到左上角）；单条 `#chara` 固定按 5 摆 |
| 选择支结果对不上 | 结果变量起点不同 | CPyMO 从 0 开始写 `FSEL`；老 PyMO 有些版本从 1 开始 |
| 鉴赏是空的 | 没有 `cgprefix` 前缀 | 见 6.1，把参战图改名或改 `cgprefix` |
| 鉴赏页数不对 | `album_list` 页码从 0 开始 | CPyMO 只认 `页码 = 当前页 + 1`，即从 1 开始 |
| 音乐鉴赏点了没反应 | `#music` 带了参数 | CPyMO 的 `#music` 不接受参数 |
| 打字速度/音量不对 | `save/config.csav` 覆盖了配置 | 删掉玩家的 `config.csav` 再试一次 |
| 满屏方框 | 字体缺字 | 见 7.3，把字体放进 `system/default.ttf` |
| 影片播不出来 | 后端没链 FFmpeg | 见 7.4 |
| 报「未知命令」但游戏能继续 | 该命令 CPyMO 没实现 | 引擎对未知命令只警告并跳过，不会崩；去 [CPyMO 指令参考.md](CPyMO%20指令参考.md) 查有没有替代写法 |
| 资源找不到但文件在 | 大小写或扩展名 | 见 9.2 第 4 条 |

### 9.4 转到 YukimiScript（可选）

如果打算长期维护这份移植，可以把文本脚本转成 YukimiScript：仓库根有 `pymo2ykm.ps1`（PowerShell，UTF-16LE 编码），它把 `#命令` 逐条翻成 `@命令`。

它**不做的**事情：

* 不认识的命令会降级成 `#` 注释，静默丢掉。转换完必须全文搜一遍 `#` 开头的行。
* 老的 `#lua` 写法不会自动改写，要手动换成 `@lua "..."`。
* `say` 变成文本行 `符号:正文`，`label` 变成 `- scene "名字"`，`goto` 变成 `@goto "名字"`。

转换后再用 `ykmc` 编译回 `script/*.txt`，游戏就能跑。这条路的好处是能用到 Feature Level 1 的 Lua 扩展和新命令；坏处是多一层工具链。小规模移植建议先保持文本脚本，跑通了再说。

---

## 10. 排错速查

**先分清楚是哪一层出的问题**：资源层（找不到文件）、脚本层（命令写错/跳错）、引擎层（崩溃/断言）、后端层（没声音/没画面/乱码）。四层的排查手段完全不同。

| 现象 | 先查 |
| --- | --- |
| 启动就退出 | `gameconfig.txt` 是否存在、`startscript` 指向的 `script/<名>.txt` 是否存在 |
| 卡在第一个画面 | 脚本第一行是不是没有可交互命令（例如只有 `#bg` 而没有 `#say`/`#waitkey`） |
| 报参数无效、某条命令没生效 | 必填参数是空的（引擎 `ENSURE` 判空后返回 `CPYMO_ERR_INVALID_ARG`）。PyMO 文本里一条命令一行写完，**没有「再补一行收尾」这种写法**；收尾行 `_do` 只属于 YukimiScript |
| 文本框位置怪异 | `msgtb` / `msglr` 与 `imagesize` 不匹配，或键名写成了字段名（见 2.2） |
| 窗口是黑的但有声音 | 图形后端初始化失败，看日志里的 `[Error]` |
| 一切正常但字很小 | `fontsize` 被 `save/config.csav` 覆盖，或逻辑分辨率定得太大 |
| 某条 `#if` 没生效 | 条件右侧写了没定义过的变量——引擎取不到就直接跳过整行，不报错（见 4.6） |
| 报 `max value must bigger than min value for rand command.` | `#rand` 的上下界相等了，把上界放大（见 4.6） |
| 立绘位置整个跑偏 | 坐标模式写成了 `cm5`，PyMO 文本里必须是纯数字 `0`–`6`（见 3.4） |
| 文字颜色变成品红 | 写了颜色名，引擎只认 `#RRGGBB`（见 3.2） |

引擎的日志直接打到标准输出，桌面平台上重定向就能存下来：

```bash
./cpymo /path/to/MyGame > game.log 2>&1
```

未知命令会打 `[Warning]`，资源加载失败会打 `[Error]`，两者都值得逐条看。

---

## 11. 接下来读什么

* 具体命令怎么用：[CPyMO 指令参考.md](CPyMO%20指令参考.md)
* YukimiScript 文法：仓库里的 `ykmc` 源码；服务器上另有一份 `YukimiScript_Wiki.md`（未随仓库分发）
* 各平台的构建与部署要求：[README.md](README.md)
* 想给游戏加无障碍（朗读、键盘导航）：README 的「视障帮助」章节，以及 `CPyMO HD.md` 里 Feature Level 的规划
