Details
===

This repo is based on https://github.com/bastibe/lunatic-python which provide a way to run lua script in python.

Main changes from the orignal repo:

* Support multiple lua states
* Support multiple return values from lua to python

Works on macOS and Linux.

---

Installing
---

```
git clone https://github.com/91Act/lunatic-python
cd lunatic-python
cmake .
make
```


Usage
---

Copy `lua.so` to one of the python `sys.path` then:

```python
import lua

lua.execute(r'''
    print('hello world!')
''')
```

