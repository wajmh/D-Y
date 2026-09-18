### 一、背景与核心痛点

1. CAN Bus-Off 触发原因：                                                           
    • 当节点检测到过多的发送错误（发送错误计数器 TEC > 255）时，硬件控制器进入 Bus- 
    Off 状态。                                                                      
    • 最典型的场景是：对端节点（如小脑上位机或                                      
    BMS）掉线/未开机，导致本地发出的报文无 ACK 应答（产生持续的 ACK Error），TEC    
    迅速累加到 255 并触发 Bus-Off。                                                 
2. 原生 HAL / 简单恢复方案的致命缺陷：                                              
    • 永久静默：原生 STM32 HAL 不提供自动脱离 Bus-Off 的机制，控制器进入 Bus-Off    
    后将彻底关闭收发，若无自动恢复，板卡只能断电重启。                              
    • 阻塞死锁：若在恢复过程中使用 HAL_Delay() 或 while()                           
    轮询寄存器，会严重阻塞电源板的放电保护、电流采样与急停检测等实时关键任务。      
    • 错误打断硬件同步（破坏 ISO 11898-1 规范）：规范要求控制器退出配置模式（CCCR.  
    INIT = 0）后，必须在总线上监听到 129 次连续的 11 个隐性位序列才能脱离 Bus-      
    Off。若软件设置了超时并反复强行把 CCCR.INIT 置 1                                
    重启硬件，在重载或存在周期干扰的总线上，硬件计数器会被频繁打断重置，导致永远无法
    完成 129 次同步，陷入永久死锁。                                                 
    • HAL 状态未更新导致发送失败：恢复后若未将 hfdcan->State 置为                   
    HAL_FDCAN_STATE_BUSY，调用 HAL_FDCAN_AddMessageToTxFifoQ() 时会被 HAL           
    库直接拒绝。                                                                    
    • 寄存器读取破坏现场：Bosch M_CAN 的协议状态寄存器（PSR）在被软件读取一次后，其 
    LEC/DLEC 错误码会自动被硬件重置为 7（No                                         
    Change），若多次读取会导致无法诊断真实的故障原因。

  ──────                                                                              

### 二、系统设计架构与恢复状态机

  整体架构采用 中断驱动 + 纯异步非阻塞状态机 + 主循环轮询兜底 的方式运行，全程 0ms    
  忙等。                                                                              

    [硬件触发 Bus-Off]                                                                
           │                                                                          
           ├──> [中断: HAL_FDCAN_ErrorStatusCallback] (单次读取PSR，代际号+1)         
           │                        │                                                 
           │ (若中断偶发丢失)          ▼                                              
           └──> [主循环兜底轮询] ──> FDCAN_BUSOFF_STATE_PENDING (等待 100ms 稳定期)   
                                    │                                                 
                                    ├──> (快速路径: 若硬件自愈/已恢复) ──> 直接恢复回 

  IDLE                                                                                
                                    │                                                 
                                    ▼ (100ms 到期)                                    
                               FDCAN_BUSOFF_STATE_REQUEST_INIT (异步请求 CCCR.INIT=1) 
                                    │                                                 
                                    ▼ (硬件确认进入 INIT)                             
                               [使能 CCE, 取消待发 TX 请求 (0x7), 清除 INIT & CCE]    
                                    │                                                 
                                    ▼                                                 
                               FDCAN_BUSOFF_STATE_START_SYNC (等待硬件退出 INIT)      
                                    │                                                 
                                    ▼ (硬件确认 CCCR.INIT=0)                          
                               FDCAN_BUSOFF_STATE_RECOVERING (异步轮询 PSR.BO == 0)   
                                    │                                                 
                                    │ (★绝不设超时打断，等待硬件自主累加 129×11 bit   
  隐性位)                                                                             
                                    ▼                                                 
                               [硬件完成同步: PSR.BO=0 && CCCR.INIT=0]                
                                    │                                                 
                                    ▼                                                 
                               [恢复 HAL 状态为 BUSY, 清零标志, 统计+1] ──>           
  FDCAN_BUSOFF_STATE_IDLE                                                             

#### 状态机流转说明：

   状态         │ 枚举值 │ 职责与转换条件
  ──────────────┼────────┼────────────────────────────────────────────────────────────
   IDLE         │ 0      │ 正常通信态。当中断或兜底检测到 PSR.BO == 1
                │        │ 时，代际序号递增，状态切入 PENDING。
   PENDING      │ 1      │ 100ms 总线稳定期。1. 快速检查：若硬件已处于正常态（BO==0
                │        │ && INIT==0），跳过后续步骤直接恢复到 IDLE；2.
                │        │ 延时到期后，请求进入配置模式（SET_BIT(CCCR, INIT)），进入
                │        │ REQUEST_INIT。
   REQUEST_INIT │ 2      │ 配置模式确认与清理。非阻塞确认 CCCR.INIT == 1：• 使能
                │        │ CCE（CCCR.CCE = 1）；• 取消所有残留在 TX FIFO
                │        │ 中的发送请求（TXBCR = FDCAN_TXBCR_CR_Msk 即有效低 3 位
                │        │ 0x7）；• 清除 CCE 和
                │        │ INIT（请求退出配置模式，硬件在此刻准备开启同步）；• 转移至
                │        │ START_SYNC。（若 10ms 硬件未响应则重退回 PENDING）。
   START_SYNC   │ 3      │ 退出配置模式确认。非阻塞确认 CCCR.INIT ==
                │        │ 0，代表硬件已退出初始化模式，正式启动 129×11
                │        │ 位隐性序列计数，进入 RECOVERING。
   RECOVERING   │ 4      │ 等待硬件协议引擎同步。极其关键：不设置超时强行打断！完全由
                │        │ 硬件在总线上监听 129 次 11 bit 隐性位。仅非阻塞轮询
                │        │ (PSR.BO == 0)，一旦硬件置零，调用 FDCAN_VerifyRecovery()
                │        │ 确认无误后恢复 HAL 状态为 BUSY，重置标志，回到 IDLE。
  ──────                                                                              

### 三、关键技术与细节处理

1. 代际号机制（Generation Counter）避免 ABA 竞态：                                  
    • 引入 volatile uint32_t fdcanX_busoff_generation。                             
    • 发生 Bus-Off                                                                  
    时代际号递增；状态机每次切状态/恢复时，比对当前代际号与局部代际号是否一致，防止“
    状态机正在恢复旧事件时，中断又上报了新事件”导致状态被旧逻辑误清除。             
2. 临界区安全恢复（PRIMASK 保存）：                                                 
    • 使用 uint32_t primask = __get_PRIMASK(); __disable_irq(); ...                 
    __set_PRIMASK(primask);                                                         
    ，确保在嵌套调用或不同中断优先级下能正确恢复原有的全局中断状态，避免无脑        
    __enable_irq() 破坏外层临界区。                                                 
3. 诊断快照冻结（一次性单次读取）：                                                 
    • 中断回调或兜底轮询中，只执行单次寄存器读取：uint32_t psr_snapshot = hfdcan-   

   > Instance->PSR;。                                                               
   >  • 保存至全局诊断变量 fdcanX_last_psr，保留完整的真实错误码（如 LEC=3 代表 ACK   
   >  Error，LEC=5 代表总线短路 Bit0 Error），避免二次读取变为 7（No Change）。       
4. 上层发送联锁保护（Tx 门禁）：                                                    
    • 在                                                                            
    FDCAN_SendCurrentReport、FDCAN_ForwardBatteryFrameToRk、FDCAN_SendBatteryAlarmRe
    portToRk、FDCAN_SendBatteryWakeFrame 等发包接口的入口处增加 busoff_flag 检测。  
    • 一旦总线处于 Bus-Off，直接返回 HAL_BUSY 或丢弃转发帧并递增                    
    drop_count，坚决不向故障通道的 TX FIFO 继续压入数据，防止 FIFO 积压溢出。       
5. 安全过滤器配置（防死锁）：                                                       
    • 将原有的过滤器配置函数改为安全返回状态：static HAL_StatusTypeDef              
    FDCAN_ConfigBatteryRxFilters(...)，发生错误时返回 HAL_ERROR 而非直接调用        
    Error_Handler() 陷入死循环。

  ──────                                                                              

### 四、在新分支上的移植操作清单

  若要在另一个分支上添加同样的 Bus-Off 恢复逻辑，请按以下 4 步进行移植：              

#### 步骤 1：修改头文件 fdcan.h

  在 /* USER CODE BEGIN Prototypes */ 区域导出自动恢复接口与遥测变量：                

    /* USER CODE BEGIN Prototypes */                                                  
    ...                                                                               
    /* Bus-Off 自动恢复接口 */                                                        
    void FDCAN_CheckAndRecoverAllBusOff(void);                                        
                                                                                      
    /* Bus-Off 恢复统计（用于调试和遥测，volatile 确保实时性） */                     
    extern volatile uint32_t fdcan1_busoff_recovery_count;                            
    extern volatile uint32_t fdcan1_busoff_recovery_fail_count;                       
    extern volatile uint32_t fdcan2_busoff_recovery_count;                            
    extern volatile uint32_t fdcan2_busoff_recovery_fail_count;                       
    extern volatile uint32_t fdcan3_busoff_recovery_count;                            
    extern volatile uint32_t fdcan3_busoff_recovery_fail_count;                       
                                                                                      
    /* Bus-Off 诊断指标（PSR/ECR 快照，用于背景遥测与健康度排查） */                  
    extern volatile uint32_t fdcan1_last_psr;                                         
    extern volatile uint32_t fdcan1_last_ecr;                                         
    extern volatile uint32_t fdcan2_last_psr;                                         
    extern volatile uint32_t fdcan2_last_ecr;                                         
    extern volatile uint32_t fdcan3_last_psr;                                         
    extern volatile uint32_t fdcan3_last_ecr;                                         
    /* USER CODE END Prototypes */                                                    

  ──────                                                                              

#### 步骤 2：在 fdcan.c 中添加核心实现

  在 USER CODE BEGIN 0 区域添加宏定义、状态机定义及处理函数：                         

1. 宏与状态机变量定义：

    /* Bus-Off 中断驱动恢复配置 */                                                    
    #define FDCAN_BUSOFF_RECOVERY_DELAY_MS 100U   /* 恢复延迟，等待总线稳定 */        
    #define FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS 10U  /* 硬件操作超时（CCCR.INIT 响应） */
                                                                                      
    typedef enum                                                                      
    {                                                                                 
      FDCAN_BUSOFF_STATE_IDLE = 0U,           /* 正常工作状态，无 Bus-Off */          
      FDCAN_BUSOFF_STATE_PENDING = 1U,        /*                                      

  等待总线稳定（100ms），优先检查是否自愈 */                                          
      FDCAN_BUSOFF_STATE_REQUEST_INIT = 2U,   /* 请求进入配置模式（CCCR.              
  INIT=1），非阻塞等待确认 */                                                         
      FDCAN_BUSOFF_STATE_START_SYNC = 3U,     /* 取消待发请求，请求退出配置模式（CCCR.
  INIT=0） */                                                                         
      FDCAN_BUSOFF_STATE_RECOVERING = 4U      /* 持续非阻塞等待 129 × 11 bit          
  隐性位同步完成 */                                                                   
    } FDCAN_BusOffState_t;                                                            

    /* 分别为 FDCAN1, FDCAN2, FDCAN3 定义恢复状态变量（根据分支实际外设数量适配） */  
    #define DEFINE_FDCAN_BUSOFF_VARS(idx) \                                           
      static volatile uint8_t fdcan##idx##_busoff_flag = 0U; \                        
      static volatile uint32_t fdcan##idx##_busoff_generation = 0U; \                 
      static volatile FDCAN_BusOffState_t fdcan##idx##_busoff_state =                 

  FDCAN_BUSOFF_STATE_IDLE; \                                                          
      static volatile uint32_t fdcan##idx##BusoffDetectTick = 0U; \                   
      static volatile uint32_t fdcan##idx##StateTick = 0U; \                          
      static volatile uint32_t fdcan##idx##SyncStartTick = 0U; \                      
      volatile uint32_t fdcan##idx##_busoff_recovery_count = 0U; \                    
      volatile uint32_t fdcan##idx##_busoff_recovery_fail_count = 0U; \               
      volatile uint32_t fdcan##idx##_last_psr = 0U; \                                 
      volatile uint32_t fdcan##idx##_last_ecr = 0U;                                   

    DEFINE_FDCAN_BUSOFF_VARS(1)                                                       
    DEFINE_FDCAN_BUSOFF_VARS(2)                                                       
    DEFINE_FDCAN_BUSOFF_VARS(3)                                                       

2. 恢复有效性判定与实例恢复状态机：

    static uint8_t FDCAN_VerifyRecovery(const FDCAN_GlobalTypeDef *instance, const    

  FDCAN_HandleTypeDef *hfdcan)                                                        
    {                                                                                 
      if ((instance == NULL) || (hfdcan == NULL)) return 0U;                          
      if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U) return 0U;                        
      if ((instance->PSR & FDCAN_PSR_BO) != 0U) return 0U;                            
      return 1U;                                                                      
    }                                                                                 

    static void FDCAN_HandleInstanceBusOff(                                           
      FDCAN_HandleTypeDef *hfdcan,                                                    
      FDCAN_GlobalTypeDef *instance,                                                  
      volatile uint8_t *busoffFlag,                                                   
      volatile uint32_t *generation,                                                  
      volatile uint32_t *detectTick,                                                  
      volatile uint32_t *stateTick,                                                   
      volatile uint32_t *syncStartTick,                                               
      volatile FDCAN_BusOffState_t *state,                                            
      volatile uint32_t *recoveryCount,                                               
      volatile uint32_t *recoveryFailCount,                                           
      volatile uint32_t *diagPsr,                                                     
      volatile uint32_t *diagEcr,                                                     
      uint32_t now)                                                                   
    {                                                                                 
      uint8_t localFlag;                                                              
      uint32_t localDetectTick, localStateTick, localGen;                             
      FDCAN_BusOffState_t localState;                                                 
      uint32_t primask;                                                               
                                                                                      
      primask = __get_PRIMASK();                                                      
      __disable_irq();                                                                
      localFlag = *busoffFlag;                                                        
      localDetectTick = *detectTick;                                                  
      localStateTick = *stateTick;                                                    
      localGen = *generation;                                                         
      localState = *state;                                                            
      __set_PRIMASK(primask);                                                         
                                                                                      
      /* 兜底检测（防御中断丢失） */                                                  
      if ((localFlag == 0U) && (localState == FDCAN_BUSOFF_STATE_IDLE))               
      {                                                                               
        uint32_t psr_snapshot = instance->PSR;                                        
        if ((psr_snapshot & FDCAN_PSR_BO) != 0U)                                      
        {                                                                             
          primask = __get_PRIMASK();                                                  
          __disable_irq();                                                            
          if (*busoffFlag == 0U)                                                      
          {                                                                           
            (*generation)++;                                                          
            *busoffFlag = 1U;                                                         
            *detectTick = now;                                                        
            *state = FDCAN_BUSOFF_STATE_PENDING;                                      
            if (diagPsr != NULL) *diagPsr = psr_snapshot;                             
            if (diagEcr != NULL) *diagEcr = instance->ECR;                            
          }                                                                           
          __set_PRIMASK(primask);                                                     
        }                                                                             
        return;                                                                       
      }                                                                               
                                                                                      
      switch (localState)                                                             
      {                                                                               
        case FDCAN_BUSOFF_STATE_PENDING:                                              
          /* 优先检查自愈 */                                                          
          if (((instance->PSR & FDCAN_PSR_BO) == 0U) && ((instance->CCCR &            

  FDCAN_CCCR_INIT) == 0U))                                                            
          {                                                                           
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            if (*generation == localGen)                                              
            {                                                                         
              hfdcan->State = HAL_FDCAN_STATE_BUSY;                                   
              hfdcan->ErrorCode = HAL_FDCAN_ERROR_NONE;                               
              hfdcan->LatestTxFifoQRequest = 0U;                                      
              *busoffFlag = 0U;                                                       
              *state = FDCAN_BUSOFF_STATE_IDLE;                                       
              FDCAN_IncrementDebugCounter(recoveryCount);                             
            }                                                                         
            __set_PRIMASK(primask);                                                   
            break;                                                                    
          }                                                                           

          /* 100ms 稳定期等待 */                                                      
          if ((now - localDetectTick) >= FDCAN_BUSOFF_RECOVERY_DELAY_MS)              
          {                                                                           
            SET_BIT(instance->CCCR, FDCAN_CCCR_INIT);                                 
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            if (*generation == localGen)                                              
            {                                                                         
              *stateTick = now;                                                       
              *state = FDCAN_BUSOFF_STATE_REQUEST_INIT;                               
            }                                                                         
            __set_PRIMASK(primask);                                                   
          }                                                                           
          break;                                                                      
                                                                                      
        case FDCAN_BUSOFF_STATE_REQUEST_INIT:                                         
          if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U)                               
          {                                                                           
            /* 清除残留 TX 队列，准备退出 INIT */                                     
            SET_BIT(instance->CCCR, FDCAN_CCCR_CCE);                                  
            instance->TXBCR = FDCAN_TXBCR_CR_Msk;  /* 0x7 */                          
            hfdcan->LatestTxFifoQRequest = 0U;                                        
                                                                                      
            CLEAR_BIT(instance->CCCR, FDCAN_CCCR_CCE);                                
            CLEAR_BIT(instance->CCCR, FDCAN_CCCR_INIT);                               
                                                                                      
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            if (*generation == localGen)                                              
            {                                                                         
              *stateTick = now;                                                       
              *state = FDCAN_BUSOFF_STATE_START_SYNC;                                 
            }                                                                         
            __set_PRIMASK(primask);                                                   
          }                                                                           
          else if ((now - localStateTick) >= FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS)        
          {                                                                           
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            *detectTick = now;                                                        
            *state = FDCAN_BUSOFF_STATE_PENDING;                                      
            __set_PRIMASK(primask);                                                   
            FDCAN_IncrementDebugCounter(recoveryFailCount);                           
          }                                                                           
          break;                                                                      
                                                                                      
        case FDCAN_BUSOFF_STATE_START_SYNC:                                           
          if ((instance->CCCR & FDCAN_CCCR_INIT) == 0U)                               
          {                                                                           
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            if (*generation == localGen)                                              
            {                                                                         
              *syncStartTick = now;                                                   
              *state = FDCAN_BUSOFF_STATE_RECOVERING;                                 
            }                                                                         
            __set_PRIMASK(primask);                                                   
          }                                                                           
          else if ((now - localStateTick) >= FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS)        
          {                                                                           
            primask = __get_PRIMASK();                                                
            __disable_irq();                                                          
            *detectTick = now;                                                        
            *state = FDCAN_BUSOFF_STATE_PENDING;                                      
            __set_PRIMASK(primask);                                                   
            FDCAN_IncrementDebugCounter(recoveryFailCount);                           
          }                                                                           
          break;                                                                      
                                                                                      
        case FDCAN_BUSOFF_STATE_RECOVERING:                                           
          /* 核心：不设超时，持续轮询等待硬件完成 129×11 bit 同步 */                  
          if ((instance->PSR & FDCAN_PSR_BO) == 0U)                                   
          {                                                                           
            if (FDCAN_VerifyRecovery(instance, hfdcan) != 0U)                         
            {                                                                         
              primask = __get_PRIMASK();                                              
              __disable_irq();                                                        
              if (*generation == localGen)                                            
              {                                                                       
                hfdcan->State = HAL_FDCAN_STATE_BUSY;                                 
                hfdcan->ErrorCode = HAL_FDCAN_ERROR_NONE;                             
                hfdcan->LatestTxFifoQRequest = 0U;                                    
                *busoffFlag = 0U;                                                     
                *state = FDCAN_BUSOFF_STATE_IDLE;                                     
                FDCAN_IncrementDebugCounter(recoveryCount);                           
              }                                                                       
              __set_PRIMASK(primask);                                                 
            }                                                                         
          }                                                                           
          break;                                                                      
                                                                                      
        default:                                                                      
          *state = FDCAN_BUSOFF_STATE_IDLE;                                           
          break;                                                                      
      }                                                                               
    }                                                                                 
                                                                                      
    void FDCAN_CheckAndRecoverAllBusOff(void)                                         
    {                                                                                 
      uint32_t now = HAL_GetTick();                                                   
      if (batteryCanStarted != 0U)                                                    
      {                                                                               
        FDCAN_HandleInstanceBusOff(&hfdcan1, FDCAN1, &fdcan1_busoff_flag,             

  &fdcan1_busoff_generation,                                                          
                                  &fdcan1BusoffDetectTick, &fdcan1StateTick,          
  &fdcan1SyncStartTick,                                                               
                                  &fdcan1_busoff_state, &fdcan1_busoff_recovery_count,
  &fdcan1_busoff_recovery_fail_count,                                                 
                                  &fdcan1_last_psr, &fdcan1_last_ecr, now);           
        FDCAN_HandleInstanceBusOff(&hfdcan2, FDCAN2, &fdcan2_busoff_flag,             
  &fdcan2_busoff_generation,                                                          
                                  &fdcan2BusoffDetectTick, &fdcan2StateTick,          
  &fdcan2SyncStartTick,                                                               
                                  &fdcan2_busoff_state, &fdcan2_busoff_recovery_count,
  &fdcan2_busoff_recovery_fail_count,                                                 
                                  &fdcan2_last_psr, &fdcan2_last_ecr, now);           
        FDCAN_HandleInstanceBusOff(&hfdcan3, FDCAN3, &fdcan3_busoff_flag,             
  &fdcan3_busoff_generation,                                                          
                                  &fdcan3BusoffDetectTick, &fdcan3StateTick,          
  &fdcan3SyncStartTick,                                                               
                                  &fdcan3_busoff_state, &fdcan3_busoff_recovery_count,
  &fdcan3_busoff_recovery_fail_count,                                                 
                                  &fdcan3_last_psr, &fdcan3_last_ecr, now);           
      }                                                                               
    }                                                                                 

3. 中断回调与中断使能：

    /* 中断回调：仅单次读取 PSR，记录代际号，置 pending 态 */                         
    void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t          

  ErrorStatusITs)                                                                     
    {                                                                                 
      uint32_t now = HAL_GetTick();                                                   
      uint32_t psr_snapshot;                                                          

      if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U)                                  
      {                                                                               
        psr_snapshot = hfdcan->Instance->PSR;                                         
        if ((psr_snapshot & FDCAN_PSR_BO) != 0U)                                      
        {                                                                             
          if (hfdcan->Instance == FDCAN1)                                             
          {                                                                           
            fdcan1_busoff_generation++;                                               
            fdcan1_busoff_flag = 1U;                                                  
            fdcan1_busoff_state = FDCAN_BUSOFF_STATE_PENDING;                         
            fdcan1BusoffDetectTick = now;                                             
            fdcan1_last_psr = psr_snapshot;                                           
            fdcan1_last_ecr = hfdcan->Instance->ECR;                                  
          }                                                                           
          else if (hfdcan->Instance == FDCAN2)                                        
          {                                                                           
            fdcan2_busoff_generation++;                                               
            fdcan2_busoff_flag = 1U;                                                  
            fdcan2_busoff_state = FDCAN_BUSOFF_STATE_PENDING;                         
            fdcan2BusoffDetectTick = now;                                             
            fdcan2_last_psr = psr_snapshot;                                           
            fdcan2_last_ecr = hfdcan->Instance->ECR;                                  
          }                                                                           
          else if (hfdcan->Instance == FDCAN3)                                        
          {                                                                           
            fdcan3_busoff_generation++;                                               
            fdcan3_busoff_flag = 1U;                                                  
            fdcan3_busoff_state = FDCAN_BUSOFF_STATE_PENDING;                         
            fdcan3BusoffDetectTick = now;                                             
            fdcan3_last_psr = psr_snapshot;                                           
            fdcan3_last_ecr = hfdcan->Instance->ECR;                                  
          }                                                                           
        }                                                                             
      }                                                                               
    }                                                                                 

  并在启动函数 FDCAN_BatteryCanStart() 中，在 HAL_FDCAN_Start() 之后，为每个 FDCAN    
  使能中断：                                                                          

    (void)HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0U);             
    (void)HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_BUS_OFF, 0U);             
    (void)HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_BUS_OFF, 0U);             

4. 发包接口加锁拦截：                                                               
     在所有的发包接口（如                                                                
     FDCAN_SendCurrentReport、FDCAN_ForwardBatteryFrameToRk、FDCAN_SendBatteryWakeFrame  
     等）入队前，检查对应的 fdcanX_busoff_flag，若非 0 则直接返回 HAL_BUSY 或丢弃，防止  
     TX FIFO 死锁。                                                                      
     ──────

#### 步骤 3：在 main.c 中轮询调用

  在主循环 while (1) 中高频调用：                                                     

      /* USER CODE BEGIN WHILE */                                                     
      while (1)                                                                       
      {                                                                               
        /* USER CODE END WHILE */                                                     
                                                                                      
        /* USER CODE BEGIN 3 */                                                       
        FDCAN_BatteryCanTask();                                                       
        Power_UpdateGpioDebugStates();                                                
        Power_DischargeModeTask();                                                    
        FDCAN_CheckAndRecoverAllBusOff(); // ★ 纯异步执行恢复状态机                   
      }                                                                               
      /* USER CODE END 3 */                                                           

  ──────                                                                              

#### 步骤 4：确认中断向量配置 stm32g4xx_it.c

  确认目标分支的 stm32g4xx_it.c 中开启了 FDCANx 的中断处理函数（通常由 CubeMX         
  自动生成，路由到 HAL_FDCAN_IRQHandler(&hfdcanx) 即可）。例如：

    void FDCAN1_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan1); }
    void FDCAN2_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan2); }
    void FDCAN3_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan3); }


