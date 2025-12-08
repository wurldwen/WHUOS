      // in fs_init()

      // inode初始化
      inode_init();

      // 获取根目录
      inode_t* ip = inode_alloc(INODE_ROOT);      
      inode_lock(ip);

      // 第一次查看
      dir_print(ip);
      
      // add entry
      dir_add_entry(ip, 1, "a.txt");
      dir_add_entry(ip, 2, "b.txt");
      dir_add_entry(ip, 3, "c.txt");
      
      // 第二次查看
      dir_print(ip);

      // 第一次检查
      assert(dir_search_entry(ip, "b.txt") == 2, "error-1");

      // delete entry
      dir_delete_entry(ip, "a.txt");
     
      // 第三次查看
      dir_print(ip);
      
      // add entry
      dir_add_entry(ip, 1, "d.txt");      
      
      // 第四次查看
      dir_print(ip);
      
      // 第二次检查
      assert(dir_add_entry(ip, 4, "d.txt") == BLOCK_SIZE, "error-2");
      
      inode_unlock(ip);

      printf("over");

      while (1); 
