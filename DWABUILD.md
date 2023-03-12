# DWA build notes
These notes are relevant only to internal DWA builds.


## Build Instructions:(rez2 only)

### Env Prep:
```
rez2 
```

### Build:
```
rez-env buildtools -c "rez-build -i -p /usr/pic1/work/rez-2"
```

### Debug Build:
```
rez-env buildtools -c "rez-build -i -p /usr/pic1/work/rez-2 -- --type=debug"
```


### Unittests:
```
rez-env buildtools -c "rez-build -- @run_all"
```


### Usage example:
```
rez2
rez-env arras_render moonbase_proxies 
arras_render -l 5 --dc local --rdl /work/pe/jose/arras/rdl/nadder.rdla --rez-packages "mcrt_computation arras4_core_impl moonshine_dwa"
```

