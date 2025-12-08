// in fs.c fs_init()
        // 在函数外声明两个个大小为 2*BLOCK_SIZE 的数组 str 和 tmp
        // blockcmp 函数负责比较两个大小为 2*BLOCK_SIZE 的空间是否完全一样

        // inode初始化
        inode_init();
        uint32 ret = 0;

        for(int i = 0; i < BLOCK_SIZE * 2; i++)
                str[i] = i;

        // 创建新的inode
        inode_t* nip = inode_create(FT_FILE, 0, 0);
        inode_lock(nip);
        
        // 第一次查看
        inode_print(nip);

        // 第一次写入
        ret = inode_write_data(nip, 0, BLOCK_SIZE / 2, str, false);
        assert(ret == BLOCK_SIZE / 2, "inode_write_data: fail");

        // 第二次写入
        ret = inode_write_data(nip, BLOCK_SIZE / 2, BLOCK_SIZE + BLOCK_SIZE / 2, str + BLOCK_SIZE / 2, false);
        assert(ret == BLOCK_SIZE +    BLOCK_SIZE / 2, "inode_write_data: fail");

        // 一次读取
        ret = inode_read_data(nip, 0, BLOCK_SIZE * 2, tmp, false);
        assert(ret == BLOCK_SIZE * 2, "inode_read_data: fail");

        // 第二次查看
        inode_print(nip);
        
        inode_unlock_free(nip);

        // 测试
        if(blockcmp(tmp, str) == true)
                printf("success");
        else
                printf("fail");

        while (1); 
