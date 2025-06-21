#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb1ad28e0, "__gnu_mcount_nc" },
	{ 0x526c3a6c, "jiffies" },
	{ 0x92997ed8, "_printk" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x7d4eaaf3, "cdev_del" },
	{ 0x4e825d26, "device_destroy" },
	{ 0x38f9d614, "class_destroy" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x39cf4f02, "gpiod_set_value" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x7335140d, "class_create" },
	{ 0x967d80b, "device_create" },
	{ 0x2d98b2f7, "cdev_init" },
	{ 0xa64ef386, "cdev_add" },
	{ 0x585c0aa5, "devm_kmalloc" },
	{ 0x1256e117, "devm_gpiod_get" },
	{ 0x2bdc277a, "_dev_err" },
	{ 0x9552e17d, "gpiod_to_irq" },
	{ 0x68cbfc63, "devm_request_threaded_irq" },
	{ 0x39d0f19b, "_dev_info" },
	{ 0x574d238a, "gpio_to_desc" },
	{ 0xb7d1045d, "gpiod_set_raw_value" },
	{ 0xc1514a3b, "free_irq" },
	{ 0xfe990052, "gpio_free" },
	{ 0xdbd6ca4c, "platform_driver_unregister" },
	{ 0xcb789b11, "__platform_driver_register" },
	{ 0xf9a482f9, "msleep" },
	{ 0x47229b5c, "gpio_request" },
	{ 0x68fd051a, "gpiod_direction_output_raw" },
	{ 0x69880269, "gpiod_direction_input" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x5f754e5a, "memset" },
	{ 0x4e99122d, "gpiod_get_value" },
	{ 0xc358aaf8, "snprintf" },
	{ 0x9c396837, "gpiod_get_raw_value" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0xe2d5255a, "strcmp" },
	{ 0xbbb36cd0, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Ccustom,gpio-control");
MODULE_ALIAS("of:N*T*Ccustom,gpio-controlC*");

MODULE_INFO(srcversion, "3594AB4938A532308B3E944");
