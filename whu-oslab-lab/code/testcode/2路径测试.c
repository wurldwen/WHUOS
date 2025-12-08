      // in fs_init()

      // inode初始化
      inode_init();

      // 创建inode
      inode_t* ip = inode_alloc(INODE_ROOT);
      inode_t* ip_1 = inode_create(FT_DIR, 0, 0);
      inode_t* ip_2 = inode_create(FT_DIR, 0, 0);
      inode_t* ip_3 = inode_create(FT_FILE, 0, 0);

      // 上锁
      inode_lock(ip);
      inode_lock(ip_1);
      inode_lock(ip_2);
      inode_lock(ip_3);

      // 创建目录
      dir_add_entry(ip, ip_1->inode_num, "user");
      dir_add_entry(ip_1, ip_2->inode_num, "work");
      dir_add_entry(ip_2, ip_3->inode_num, "hello.txt");
      
      // 填写文件
      inode_write_data(ip_3, 0, 11, "hello world", false);

      // 解锁
      inode_unlock(ip_3);
      inode_unlock(ip_2);
      inode_unlock(ip_1);
      inode_unlock(ip);

      // 路径查找
      char* path = "/user/work/hello.txt";
      char name[DIR_NAME_LEN];
      inode_t* tmp_1 = path_to_pinode(path, name);
      inode_t* tmp_2 = path_to_inode(path);

      assert(tmp_1 != NULL, "tmp1 = NULL");
      assert(tmp_2 != NULL, "tmp2 = NULL");
      printf("\nname = %s\n", name);

      // 输出 tmp_1 的信息
      inode_lock(tmp_1);
      inode_print(tmp_1);
      inode_unlock_free(tmp_1);

      // 输出 tmp_2 的信息
      inode_lock(tmp_2);
      inode_print(tmp_2);
      char str[12];
      str[11] = 0;
      inode_read_data(tmp_2, 0, tmp_2->size, str, false);
      printf("read: %s\n", str);
      inode_unlock_free(tmp_2);

      printf("over");
      while (1); 
```
