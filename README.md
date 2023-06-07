# varserver

The Variable Server is a publish/subscribe in-memory key/value store.  It runs as a service, with communication via a shared object library.  Client processes connect to the server and can create/set/get/wait variables.

The examples below show simple varserver uses cases, but the real power of
the variable server is when it is integrated into your own applications and used for inter-process-communication in a multi-process system.

Some features are still a work-in-progress such as tags, flags, permissions, and multi-instancing of variables.

## Build the variable server

```
$ ./build.sh
```

## Start the Variable Server

```
$ varserver &
```

## Create some test variables

```
$ mkvar -n /sys/test/f -t float -F %0.2f

$ mkvar /sys/test/c

$ mkvar -n /sys/test/a -t uint16

$ mkvar -n /sys/test/b -t int32

```

## Set variable values

```
$ setvar /sys/test/c "Hello World"
OK

$ setvar /sys/test/f 1.2
OK

$ setvar /sys/test/a 15
OK

$ setvar /sys/test/b -3
OK
```

## Get variable values

```
$ getvar /sys/test/c
Hello World

$ getvar /sys/test/f
1.20

$ getvar /sys/test/b
-3

```

## Dump all variables

```
$ vars -v
/sys/test/f=1.20
/sys/test/c=Hello World
/sys/test/a=15
/sys/test/b=-3
```

## Query variables

```
$ vars -vn /test/
/sys/test/f=1.20
/sys/test/c=Hello World
/sys/test/a=15
/sys/test/b=-3
```

## Template Rendering

The varserver library supports template rendering.  Variable names embedded within the template
in the form ${varname} will be replaced with the variable value when the template is rendered
into an output file.

A template file can be rendered into an output file using the vartemplate utility.

An example template is shown below:

```
------------------------------------------------------------------------------

This is a test of the variable server templating system.

It allows referencing of variables inside a template.

The variables will be replaced with their values rendered by the
variable server.

The value of "/sys/test/a" is "${/sys/test/a}" and the value of "/sys/test/b"
is "${/sys/test/b}".

It can handle variables of all types.

This one is a string: "${/sys/test/c}" and this one is a float: ${/sys/test/f}

------------------------------------------------------------------------------
```

It can be rendered into its final output as follows:

vartemplate `template_file_name`

For example:
```
$ vartemplate vartemplate/test/template.txt
------------------------------------------------------------------------------

This is a test of the variable server templating system.

It allows referencing of variables inside a template.

The variables will be replaced with their values rendered by the
variable server.

The value of "/sys/test/a" is "15" and the value of "/sys/test/b"
is "-3".

It can handle variables of all types.

This one is a string: "Hello World" and this one is a float: 1.20

------------------------------------------------------------------------------
```


