# Plan 28 step 0.1 (Gerald, 2026-10-16): "there should be only one declaration per line, this is way more readable by
# a human".
#
# THE PROPERTY. No declaration statement declares two names (`T a, b;`, `T a = 1, b = 2;`, `T a{…}, b{…};`,
# `T a(…), b(…);`, `const T* a, *b;`, `int f(int), g(int);`, at namespace, class or block scope), and no line holds
# two declaration statements of one scope (`Input x{…}; Output y{…};`, `struct P { int a; int b; };`). A statement
# whose declarators are spread over lines (`int a = 1,` then `b = 2;`) declares two names all the same and is refused
# too, at the line of its first extra comma. Allowed: a `for (…;…;…)` header and anything else in parentheses (a
# parameter list, wrapped or not, a call, an `if`'s init); commas in `{}`, `[]` or a template's `<…>` (an initializer
# list, an enumerator list, a capture, template arguments, also in an initializer); a structured binding; a one-line
# body that declares at most one name; a constructor's initializer list and a class's base list; `using`, `typedef`,
# `return a, b;` and every other statement that is no declaration (`a = 1, b = 2;`); preprocessor lines.
#
# HOW THE FILE IS READ. Comments, string, character and raw string literals and preprocessor lines are blanked first,
# from the left (whichever of `//`, `/*`, `"`, `'`, `R"` or `#` comes first decides, so a `/*` in a string or a `'"'`
# opens nothing; a `'` after a digit is a separator), keeping every newline, so a line number is the file's. A few
# rewrites follow, each one REGEX REPLACE over the whole text: `a::b` and `operator<` become one name, `x.member` `x`,
# every number `0`; a `<` right after a name (or `template`) opens template arguments, so `a < b`, spaced as
# clang-format spaces it, is a comparison; a line that holds only an all-caps macro's call gets the `;` its expansion
# has; and what a group closed on its own line holds (`(…)`, `[…]`, `<…>`, a `{…}` without a `;`) is emptied, since it
# never declares at the statement's level. The text is then cut into tokens, newlines among them, by one REGEX
# MATCHALL; it never becomes a list before that, for the reason structure_limits.cmake's header gives (`;`, `[` and
# `]` are renamed first: a CMake list splits at the one and does not split inside the others).
#
# HOW A STATEMENT IS READ. Brackets nest on a stack. A `{` opens a scope (statements follow) after a `)` with only
# specifiers or a trailing return type since, after a lambda's `]`, at a statement's start, after a class or namespace
# head or `if consteval`, or after the `}` of a braced member initializer; any other `{` (after a name, a `=`, a `(`, a
# `,`, an enum head) holds an expression. At a scope's level, a statement that begins with optional specifiers, a type
# (names, `::`, template arguments, builtin words, `*`, `&`) and then a name is a declaration, and a comma at its level
# is a second declarator. A scope opened inside a declaration's initializer (a lambda) is read on its own and the
# declaration resumes after it. A name followed by `:` at a statement's start is a label.
#
# WHAT IT CANNOT SEE (false negatives, accepted):
#   - an elaborated type at the start (`struct S a, b;`, `enum E a, b;`) and declarators in parentheses
#     (`void (*f)(), (*g)();`): the statement reads as something else;
#   - `struct S { … } a, b;`: the names after a class body are not read as a declaration;
#   - a comparison written without spaces (`bool lo = a<b, hi = c;`): the `<` is read as a template's, the comma as
#     its argument separator;
#   - two declarations on one line in two different scopes (`int a = 1; { int b = 2; }`), and two `using`, `enum` or
#     class definitions on one line (they are no declaration statements here);
#   - a call of a macro that is not all capitals, or not alone on its line, and expands to a statement without its
#     `;`: it swallows the declaration that follows it;
#   - a `/*` opened in a preprocessor line and closed on a later line;
#   - a variable template with a `requires` clause
#     (`template <class T> requires C<T> inline constexpr int a = 1, b = 2;`), a function-like macro used as a
#     specifier (`ALIGNED(64) float a, b;`), and a space before a template's `<` (`std::vector <int> a, b;`, which
#     reads as a comparison);
# and it wrongly refuses a comma expression that starts with a product or a bitwise and (`x * y, z;`, `a & b, c;`),
# which reads as a declaration of a pointer or a reference. All rare; the tree has none.
# A file whose brackets do not balance stops the gate: reporting it clean would be a false pass.
#
# SPEED. About 8 s over the tree's 106,000 lines, the rate of structure_limits.cmake. CMake records every command of
# an if() block each time it reaches one, so a chain of if()s per token cost four times as much: each token is read by
# a small macro named by a table from its hex spelling, and most of them only look their next state up in a table.
#
# Run with:  cmake "-DTREES=<dir or file>;…" "-DEXEMPT=<path>;…" -P one_declaration_per_line.cmake

cmake_minimum_required(VERSION 3.20) # CMP0054: a token such as `_st` read from a source is never taken for a variable

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

set(_sources "")
foreach(tree IN LISTS TREES)
    if(IS_DIRECTORY "${tree}")
        file(GLOB_RECURSE _found "${tree}/*.hpp" "${tree}/*.cpp" "${tree}/*.h" "${tree}/*.cc")
        list(APPEND _sources ${_found})
    elseif(EXISTS "${tree}")
        list(APPEND _sources "${tree}")
    else()
        message(FATAL_ERROR "one declaration per line: ${tree} does not exist")
    endif()
endforeach()
set(_kept "")
foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
    set(_skip FALSE)
    foreach(exempt IN LISTS EXEMPT)
        get_filename_component(_exempt_absolute "${exempt}" ABSOLUTE)
        string(FIND "${_absolute}/" "${_exempt_absolute}/" _at) # a prefix, read literally: a path is no regex
        if(_at EQUAL 0)
            set(_skip TRUE)
        endif()
    endforeach()
    if(NOT _skip)
        list(APPEND _kept "${_absolute}")
    endif()
endforeach()
list(LENGTH _kept _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "one declaration per line: no sources — a gate that scanned nothing reports the same "
                        "'passed' as one that scanned everything")
endif()

# ---- the blanking's rarer cases -------------------------------------------------------------------------------------
macro(_blank_block_comment) # to the `*/`, searched after the `/*`
    string(SUBSTRING "${_rest}" 2 -1 _rest)
    string(FIND "${_rest}" "*/" _end)
    if(_end LESS 0)
        set(_body "${_rest}")
        set(_rest "")
    else()
        string(SUBSTRING "${_rest}" 0 ${_end} _body)
        math(EXPR _end "${_end}+2")
        string(SUBSTRING "${_rest}" ${_end} -1 _rest)
    endif()
    string(REGEX REPLACE "[^\n]" "" _body "${_body}")
    string(APPEND _text "${_pre} ${_body}")
endmacro()

macro(_blank_directive) # on over lines that end in a backslash
    set(_body "")
    set(_running 1)
    while(_running)
        string(FIND "${_rest}" "\n" _end)
        if(_end LESS 0)
            set(_rest "")
            set(_running 0)
        else()
            string(SUBSTRING "${_rest}" 0 ${_end} _directive)
            string(SUBSTRING "${_rest}" ${_end} -1 _rest)
            if(_directive MATCHES "\\\\$")
                string(APPEND _body "\n")
                string(SUBSTRING "${_rest}" 1 -1 _rest)
            else()
                set(_running 0)
            endif()
        endif()
    endwhile()
    string(APPEND _text "${_pre} ${_body}")
endmacro()

# A literal. Each MATCHES of an if() clears the matches of the one before, even a failing one, so each literal is
# matched in an if() of its own.
macro(_blank_literal)
    set(_close "")
    set(_literal "")
    if(_mark STREQUAL "\"" AND _pre MATCHES "(^|[^A-Za-z0-9_])(u8|[uUL])?R$")
        if(_rest MATCHES "^\"([^ ()\\\\\t\n]*)\\(") # a raw string, to its `)delimiter"`
            set(_close ")${CMAKE_MATCH_1}\"")
            string(LENGTH "${CMAKE_MATCH_0}" _open)
        endif()
    endif()
    if(NOT _close STREQUAL "")
        string(REGEX REPLACE "(u8|[uUL])?R$" "" _pre "${_pre}")
        string(SUBSTRING "${_rest}" ${_open} -1 _rest)
        string(FIND "${_rest}" "${_close}" _end)
        if(_end LESS 0)
            set(_literal "${_rest}")
            set(_rest "")
        else()
            string(LENGTH "${_close}" _length)
            math(EXPR _length "${_end}+${_length}")
            string(SUBSTRING "${_rest}" 0 ${_length} _literal)
            string(SUBSTRING "${_rest}" ${_length} -1 _rest)
        endif()
    else()
        if(_mark STREQUAL "\"")
            if(_rest MATCHES "^\"([^\"\\\\\n]|\\\\.)*\"")
                set(_literal "${CMAKE_MATCH_0}")
            endif()
        elseif(_rest MATCHES "^'([^'\\\\\n]|\\\\.)*'")
            set(_literal "${CMAKE_MATCH_0}")
        endif()
        if(_literal STREQUAL "") # a quote that closes on no line: read as a blank
            set(_literal "${_mark}")
        endif()
        string(LENGTH "${_literal}" _length)
        string(SUBSTRING "${_rest}" ${_length} -1 _rest)
    endif()
    string(REGEX REPLACE "[^\n]" "" _literal "${_literal}") # its newlines: an escaped one, a raw string's
    string(APPEND _text "${_pre} ${_literal}")
endmacro()

# ---- the walk --------------------------------------------------------------------------------------------------------
# Each token is read by one small macro, named by a table from the token's hex spelling (`_on` for a name, `_on_comma`
# for a `,`); most only look their next state up in a table (SPEED, above). The state of the statement so far:
#   _st    S start, Sp after alignas, T a type, Tb a builtin type word, Ta `auto`, Ts after `::`, Tp after decltype,
#          D a declaration (Dn as it begins), Di in its initializer, DD a declaration that declared two names,
#          O no declaration, Os a class or namespace head, Oe an enum's, Or after `requires`, L a label, C a control
#          before its parentheses, N inside brackets (the statement's own state waits on _stack)
#   _prev  r right after a `)` or a lambda's `]` (only specifiers since), a after a `->` or `requires` there, i a name,
#          z an array's `]`, g template arguments, c a `}`, x anything else
#   _stack per open bracket its kind (p `(`, a `[`, l a lambda's `[`, t `<`, e an expression's `{`, s a scope's `{`) and
#          the state it closes back to; letters, since a `[` in a CMake list would stop it splitting
set(_states S Sp T Tb Ta Ts Tp D Di DD O Os Oe Or L C N)
set(_types T Tb Ta Ts Tp)
macro(_table name) # _table(<name> <state>=<state> …): every state not named stays
    foreach(_from IN LISTS _states)
        set(_t_${name}_${_from} ${_from})
    endforeach()
    foreach(_pair IN ITEMS ${ARGN})
        string(REPLACE "=" ";" _pair "${_pair}")
        list(GET _pair 0 _from)
        list(GET _pair 1 _to)
        set(_t_${name}_${_from} ${_to})
    endforeach()
endmacro()
_table(name S=T Sp=T T=Dn Tb=Dn Ta=Dn Ts=T Tp=T)
_table(builtin S=Tb Sp=Tb T=Dn Ta=Dn Ts=Tb Tp=Tb)
_table(auto S=Ta Sp=Ta)
_table(typeof S=Tp Sp=Tp)
_table(attr S=Sp)
_table(head S=Os Or=Os)
_table(enum S=Oe)
_table(control S=C)
_table(label S=L)
_table(requires S=Or)
_table(other S=O Sp=O)
_table(parens S=O Sp=S T=O Tb=O Ta=O Ts=O Tp=T C=S)
_table(open S=O Sp=S T=O Tb=O Ta=O Ts=O Tp=T)
_table(close C=S)
_table(semicolon S=S Sp=S T=S Tb=S Ta=S Ts=S Tp=S D=S Di=S DD=S O=S Os=S Oe=S Or=S L=S C=S)
_table(assign S=O Sp=O T=O Tb=O Ta=O Ts=O Tp=O D=Di)
_table(comma S=O Sp=O T=O Tb=O Ta=O Ts=O Tp=O D=DD Di=DD)
_table(scope S=Ts T=Ts Tb=Ts Ta=Ts Tp=Ts)
_table(bracket T=O Tb=O Ts=O Tp=O)
_table(star S=O Sp=O)
_table(colon L=S T=S S=O Sp=O Tb=O Ta=O Ts=O Tp=O) # `done:` is a label
_table(op S=O Sp=O T=O Tb=O Ta=O Ts=O Tp=O)
_table(expression T=O Tb=O Ta=O Ts=O Tp=O)                  # a `{…}` after a type: `T{…}`, a temporary
_table(body S=S Sp=S T=S Tb=S Ta=S Ts=S Tp=S D=S O=S Os=S Oe=S Or=S L=S C=S) # a body ends its statement, a lambda's not
foreach(_from IN ITEMS r a i z g c x)
    set(_p_name_${_from} i)
    set(_p_scope_${_from} x)  # `::`, `*`, `<`
    set(_p_angle_${_from} g)  # template arguments closed
    set(_p_arrow_${_from} x)
    set(_kind_a_${_from} l)   # a `[` after it: a lambda's, after a name, a `)`, a `]` or a `>` an array's
endforeach()
foreach(_p IN ITEMS name scope angle arrow)
    set(_p_${_p}_a a)         # a trailing return type goes on
endforeach()
set(_p_arrow_r a)
foreach(_from IN ITEMS i r z g)
    set(_kind_a_${_from} a)
endforeach()
set(_p_bracket_l r)
set(_p_bracket_a z)
# What a `{` opens, by state and _prev: a scope after a `)` or a lambda's `]` with only specifiers since, at a
# statement's start, after a class or namespace head, after `if consteval`, after a braced member initializer's `}`;
# else an expression.
foreach(_st IN LISTS _states)
    foreach(_p IN ITEMS r a i z g c x)
        set(_kind e)
        if(_p MATCHES "^[ra]$" OR (_st MATCHES "^(S|Os|C)$") OR (_p STREQUAL "c" AND NOT _st MATCHES "^(N|Oe)$"))
            set(_kind s)
        endif()
        set(_kind_${_st}${_p} ${_kind})
    endforeach()
endforeach()

macro(_classify class)
    foreach(_token IN ITEMS ${ARGN})
        string(HEX "${_token}" _h)
        set(_class_${_h} ${class})
    endforeach()
endmacro()
_classify(_keep static inline thread_local constinit extern virtual explicit friend typename register template export
          const volatile mutable constexpr consteval noexcept override final else do try) # also between `)` and `{`
_classify(_attr alignas __attribute__ __declspec)
_classify(_typeof decltype typeof __typeof__)
_classify(_builtin void bool char wchar_t char8_t char16_t char32_t short int long signed unsigned float double)
_classify(_auto auto)
_classify(_head struct class union namespace)
_classify(_enum enum)
_classify(_control if for while switch catch)
_classify(_label case default public private protected)
_classify(_requires requires)
_classify(_other return co_return co_yield co_await throw goto break continue using typedef static_assert delete new
          this sizeof alignof typeid true false nullptr concept asm operator)
_classify(_nl "\n")
_classify(_parens "()")
_classify(_braces "{}")
_classify(_template "#>")
_classify(_brackets "$`")
_classify(_semicolon "@")
_classify(_assign "=")
_classify(_open_brace "{")
_classify(_close_brace "}")
_classify(_comma ",")
_classify(_open_paren "(")
_classify(_close_paren ")")
_classify(_open_angle "#")
_classify(_close_angle ">")
_classify(_open_bracket "$")
_classify(_close_bracket "`")
_classify(_scope "::")
_classify(_arrow "->")
_classify(_star "*")
_classify(_amp "&" "&&")
_classify(_ellipsis "...")
_classify(_colon ":")
_classify(_op "0" "." ">=" "-" "+" "/" "%" "|" "^" "!" "?" "~" "<")

macro(_flag) # the current line, once
    if(NOT _last_flag EQUAL _number)
        list(APPEND _flagged ${_number})
        set(_last_flag ${_number})
    endif()
endmacro()

macro(_declare) # a statement's first declarator; the scope's second on one line is an offence
    set(_st D)
    if(_last_decl EQUAL _number)
        _flag()
    endif()
    set(_last_decl ${_number})
endmacro()

macro(_broke what)
    set(_broken "${_number}: ${what}")
    break()
endmacro()

macro(_pop) # the innermost bracket: its kind, and the state it closes back to
    list(POP_BACK _stack _top)
    string(SUBSTRING "${_top}" 0 1 _kind)
    string(SUBSTRING "${_top}" 1 -1 _st)
endmacro()

macro(_drop_angles) # the `<` on top that closed nothing were comparisons
    while(_angles GREATER 0)
        list(GET _stack -1 _top)
        if(NOT _top MATCHES "^t")
            break()
        endif()
        _pop()
        math(EXPR _angles "${_angles}-1")
    endwhile()
endmacro()

macro(_on_nl)
    math(EXPR _number "${_number}+1")
endmacro()

macro(_on) # a name
    set(_st "${_t_name_${_st}}")
    if(_st STREQUAL "Dn")
        _declare()
    endif()
    set(_prev "${_p_name_${_prev}}")
endmacro()

macro(_on_builtin)
    set(_st "${_t_builtin_${_st}}")
    if(_st STREQUAL "Dn")
        _declare()
    endif()
    set(_prev "${_p_name_${_prev}}")
endmacro()

macro(_on_keep) # a specifier: the statement's start, its type and a `)`'s body stay
endmacro()

macro(_on_auto)
    set(_st "${_t_auto_${_st}}")
    set(_prev "${_p_name_${_prev}}")
endmacro()

macro(_on_typeof)
    set(_st "${_t_typeof_${_st}}")
    set(_prev "${_p_name_${_prev}}")
endmacro()

macro(_on_attr)
    set(_st "${_t_attr_${_st}}")
    set(_prev x)
endmacro()

macro(_on_head)
    set(_st "${_t_head_${_st}}")
    set(_prev x)
endmacro()

macro(_on_enum)
    set(_st "${_t_enum_${_st}}")
    set(_prev x)
endmacro()

macro(_on_control)
    set(_st "${_t_control_${_st}}")
    set(_prev x)
endmacro()

macro(_on_label)
    set(_st "${_t_label_${_st}}")
    set(_prev x)
endmacro()

macro(_on_requires)
    set(_st "${_t_requires_${_st}}")
    set(_prev "${_p_arrow_${_prev}}")
endmacro()

macro(_on_other)
    set(_st "${_t_other_${_st}}")
    set(_prev x)
endmacro()

macro(_on_parens) # `()`: a call, a cast, a parameter list, a control's condition
    set(_st "${_t_parens_${_st}}")
    set(_prev r)
endmacro()

macro(_on_semicolon)
    if(_angles GREATER 0)
        _drop_angles()
    endif()
    set(_st "${_t_semicolon_${_st}}")
    set(_prev x)
endmacro()

macro(_on_assign)
    set(_st "${_t_assign_${_st}}")
    set(_prev x)
endmacro()

macro(_on_comma)
    if(_st MATCHES "^Di?$") # a second declarator
        _flag()
    endif()
    set(_st "${_t_comma_${_st}}")
    set(_prev x)
endmacro()

macro(_on_braces) # `{}`: a scope or an expression opened and closed on its line, holding no `;`
    if(_kind_${_st}${_prev} STREQUAL "s")
        set(_st "${_t_body_${_st}}")
    else()
        set(_st "${_t_expression_${_st}}")
    endif()
    set(_prev c)
endmacro()

macro(_on_open_brace)
    if(_kind_${_st}${_prev} STREQUAL "s")
        list(APPEND _stack "s${_t_body_${_st}}")
        list(APPEND _decls ${_last_decl})
        set(_st S)
        set(_last_decl 0)
    else()
        list(APPEND _stack "e${_t_expression_${_st}}")
        set(_st N)
    endif()
    set(_prev x)
endmacro()

macro(_on_close_brace)
    if(_angles GREATER 0)
        _drop_angles()
    endif()
    _pop()
    if(_kind STREQUAL "s")
        list(POP_BACK _decls _last_decl)
    elseif(NOT _kind STREQUAL "e")
        _broke("a `}` closes `${_top}`")
    endif()
    set(_prev c)
endmacro()

macro(_on_template) # `<…>`
    set(_prev "${_p_angle_${_prev}}")
endmacro()

macro(_bracket) # `[`: a lambda's (l), else an array's, a subscript's or a structured binding's (a)
    set(_kind "${_kind_a_${_prev}}")
    if(_st STREQUAL "Ta")
        set(_kind a)
        _declare()
    else()
        set(_st "${_t_bracket_${_st}}")
    endif()
endmacro()

macro(_on_brackets) # `[…]`
    _bracket()
    set(_prev "${_p_bracket_${_kind}}")
endmacro()

macro(_on_scope) # `::` after template arguments or at a statement's start (`a::b` is one name)
    set(_st "${_t_scope_${_st}}")
    set(_prev "${_p_scope_${_prev}}")
endmacro()

# Brackets left open on their line: a wrapped parameter list, call, initializer or template argument list.
macro(_on_open_paren)
    list(APPEND _stack "p${_t_open_${_st}}")
    set(_st N)
    set(_prev x)
endmacro()

macro(_on_close_paren)
    if(_angles GREATER 0)
        _drop_angles()
    endif()
    _pop()
    if(NOT _kind STREQUAL "p")
        _broke("a `)` closes `${_top}`")
    endif()
    set(_st "${_t_close_${_st}}")
    set(_prev r)
endmacro()

macro(_on_open_bracket)
    _bracket()
    list(APPEND _stack "${_kind}${_st}")
    set(_st N)
    set(_prev x)
endmacro()

macro(_on_close_bracket)
    if(_angles GREATER 0)
        _drop_angles()
    endif()
    _pop()
    if(NOT _kind MATCHES "^[al]$")
        _broke("a `]` closes `${_top}`")
    endif()
    set(_prev "${_p_bracket_${_kind}}")
endmacro()

macro(_on_open_angle)
    list(APPEND _stack "t${_st}")
    set(_st N)
    math(EXPR _angles "${_angles}+1")
    set(_prev "${_p_scope_${_prev}}")
endmacro()

macro(_on_close_angle)
    set(_top "")
    if(_angles GREATER 0)
        list(GET _stack -1 _top)
    endif()
    if(_top MATCHES "^t")
        _pop()
        math(EXPR _angles "${_angles}-1")
        set(_prev "${_p_angle_${_prev}}")
    else() # a comparison
        set(_st "${_t_op_${_st}}")
        set(_prev x)
    endif()
endmacro()

macro(_on_arrow)
    set(_st "${_t_op_${_st}}")
    set(_prev "${_p_arrow_${_prev}}")
endmacro()

macro(_on_star) # part of a type
    set(_st "${_t_star_${_st}}")
    set(_prev "${_p_scope_${_prev}}")
endmacro()

macro(_on_amp) # part of a type, or a `)`'s ref-qualifier
    set(_st "${_t_star_${_st}}")
endmacro()

macro(_on_ellipsis) # part of a type
    set(_st "${_t_star_${_st}}")
    set(_prev x)
endmacro()

macro(_on_colon)
    set(_st "${_t_colon_${_st}}")
    set(_prev x)
endmacro()

macro(_on_op) # a number, `.`, an operator
    set(_st "${_t_op_${_st}}")
    set(_prev x)
endmacro()

set(_offences "")
set(_count 0)

foreach(source IN LISTS _kept)
    file(READ "${source}" _contents)
    string(REPLACE "\r" "" _contents "${_contents}")

    # ---- blank comments, literals and preprocessor lines, keeping every newline ----------------------------------
    set(_text "")
    set(_rest "${_contents}")
    while(TRUE)
        if(NOT _rest MATCHES "^([^/\"'#]*)([/\"'#])(.?)")
            string(APPEND _text "${_rest}")
            break()
        endif()
        set(_pre "${CMAKE_MATCH_1}")
        set(_mark "${CMAKE_MATCH_2}")
        set(_next "${CMAKE_MATCH_3}")
        string(LENGTH "${_pre}" _at)
        string(SUBSTRING "${_rest}" ${_at} -1 _rest)
        if(_mark STREQUAL "/" AND _next STREQUAL "/") # to the end of the line
            string(FIND "${_rest}" "\n" _end)
            if(_end LESS 0)
                set(_rest "")
            else()
                string(SUBSTRING "${_rest}" ${_end} -1 _rest)
            endif()
            string(APPEND _text "${_pre} ")
            continue()
        endif()
        if(_mark STREQUAL "/")
            if(_next STREQUAL "*")
                _blank_block_comment()
            else()
                string(SUBSTRING "${_rest}" 1 -1 _rest)
                string(APPEND _text "${_pre}/")
            endif()
            continue()
        endif()
        if(_mark STREQUAL "#")
            _blank_directive()
            continue()
        endif()
        if(_mark STREQUAL "'" AND _pre MATCHES "[A-Za-z0-9_]$" AND NOT _pre MATCHES "(^|[^A-Za-z0-9_])(u8|[uUL])$")
            string(SUBSTRING "${_rest}" 1 -1 _rest) # a digit separator
            string(APPEND _text "${_pre}")
            continue()
        endif()
        _blank_literal()
    endwhile()

    # ---- tokens: the rewrites of the header's HOW THE FILE IS READ ---------------------------------------------------
    # A template's `<` becomes `#` (not `<<` or `<=`); `;`, `[` and `]` become `@`, `$` and a backtick.
    string(REPLACE "\\" " " _text "${_text}")
    string(REGEX REPLACE "(^|[^A-Za-z0-9_])operator[ \t]*(\\(\\)|\\[\\]|[-+*/%^&|~!=<>,]+)" "\\1operator_" _text
           "${_text}")
    string(REGEX REPLACE "(^|[^A-Za-z0-9_])template[ \t]+<" "\\1template<" _text "${_text}")
    string(REGEX REPLACE "([A-Za-z0-9_]|\\])<" "\\1#" _text "${_text}") # a generic lambda's `]<` opens one too
    string(REPLACE "#<" "<<" _text "${_text}")
    string(REPLACE "#=" "<=" _text "${_text}")
    string(REGEX REPLACE "([A-Za-z0-9_])::" "\\1__" _text "${_text}")
    string(REGEX REPLACE "\\.(template[ \t]+)?[A-Za-z_][A-Za-z0-9_]*" "" _text "${_text}")
    string(REPLACE ";" "@" _text "${_text}")
    string(REPLACE "[" "$" _text "${_text}")
    string(REPLACE "]" "`" _text "${_text}")
    string(REGEX REPLACE "(^|[^A-Za-z0-9_])[0-9][A-Za-z0-9_.]*" "\\1 0" _text "${_text}") # every number is `0`
    # Groups closed on their line, emptied innermost first: the walk then reads a third of the tokens it would.
    set(_was "")
    while(NOT _was STREQUAL _text)
        set(_was "${_text}")
        string(REGEX REPLACE "\\(([^(){}\n]|\\(\\)|{})*\\)" "()" _text "${_text}")
        string(REGEX REPLACE "{([^(){}\n@]|\\(\\)|{})*}" "{}" _text "${_text}")
        string(REGEX REPLACE "#([^#>(){}\n@$`]|\\(\\)|{}|#>|\\$`)*>" "#>" _text "${_text}")
        string(REGEX REPLACE "\\$([^$`(){}\n@]|\\(\\)|{}|\\$`)*`" "$`" _text "${_text}")
    endwhile()
    # A line that holds only an all-caps macro's call (`SHAPE(1, 300, 100)`) ends a statement, as its expansion does.
    foreach(_pass 1 2) # a run of them takes two: each match takes its line's newline from the next one
        string(REGEX REPLACE "(\n[ \t]*[A-Z][A-Z0-9_]*\\(\\)[ \t]*)\n" "\\1@\n" _text "${_text}")
    endforeach()
    string(REGEX MATCHALL
           "\n|\\(\\)|{}|#>|\\$`|[A-Za-z_][A-Za-z0-9_]*|0|::|->|\\.\\.\\.|&&|>=|[(){}@$`#>,=*&:.]|[-+/%|^!?~<]"
           _tokens "${_text}")

    # ---- statements -------------------------------------------------------------------------------------------------
    set(_number 1)
    set(_st S)
    set(_prev x)
    set(_stack "")
    set(_angles 0)      # `t` entries on _stack
    set(_decls "")      # per open scope, the line of the last declaration around it
    set(_last_decl 0)   # the line of this scope's last declaration
    set(_flagged "")
    set(_last_flag 0)
    set(_broken "")
    foreach(_t IN LISTS _tokens)
        string(HEX "${_t}" _h)
        cmake_language(CALL "_on${_class_${_h}}")
    endforeach()

    # The walk is the part most likely to be quietly wrong, so it says so when it is: brackets that do not balance
    # mean every statement after the slip was read in the wrong place.
    list(LENGTH _stack _open)
    if(_broken STREQUAL "" AND _open GREATER 0)
        set(_broken "${_number}: ${_open} brackets are still open at the end")
    endif()
    if(NOT _broken STREQUAL "")
        message(FATAL_ERROR "one declaration per line: cannot read ${source}:${_broken}. Most likely a macro or a "
                            "preprocessor branch that opens a bracket another closes; reporting the file clean would "
                            "be a false pass.")
    endif()

    # The flagged lines' text, peeled off the front one at a time (structure_limits.cmake says why).
    list(LENGTH _flagged _flags)
    if(_flags GREATER 0)
        math(EXPR _count "${_count}+${_flags}")
        list(GET _flagged 0 _want)
        set(_index 0)
        set(_line_number 0)
        set(_rest "${_contents}")
        while(_index LESS _flags)
            math(EXPR _line_number "${_line_number}+1")
            string(FIND "${_rest}" "\n" _break)
            if(_break LESS 0)
                set(_raw "${_rest}")
            else()
                string(SUBSTRING "${_rest}" 0 ${_break} _raw)
                math(EXPR _break "${_break}+1")
                string(SUBSTRING "${_rest}" ${_break} -1 _rest)
            endif()
            if(_line_number EQUAL _want)
                string(STRIP "${_raw}" _raw)
                string(APPEND _offences "\n  ${source}:${_line_number}: ${_raw}")
                math(EXPR _index "${_index}+1")
                if(_index LESS _flags)
                    list(GET _flagged ${_index} _want)
                endif()
            endif()
        endwhile()
    endif()
endforeach()

if(_count GREATER 0)
    message(FATAL_ERROR
        "one declaration per line (Plan 28, Gerald 2026-10-16):${_offences}\n(${_count} in all)\n"
        "Give each name its own declaration, on its own line, with its type, qualifiers and initializer as written.")
endif()

message(STATUS "one declaration per line: ${_source_count} files, none declares two names")
