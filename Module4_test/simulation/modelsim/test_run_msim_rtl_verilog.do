transcript on
if {[file exists rtl_work]} {
	vdel -lib rtl_work -all
}
vlib rtl_work
vmap work rtl_work

vlog -vlog01compat -work work +incdir+C:/AlteraPrj/Project3/Module4_test {C:/AlteraPrj/Project3/Module4_test/testtop.v}
vlog -vlog01compat -work work +incdir+C:/AlteraPrj/Project3/Module4_test {C:/AlteraPrj/Project3/Module4_test/test.v}

