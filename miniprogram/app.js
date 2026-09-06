// 龙顺庄园·会议中心 小程序全局逻辑
App({
  globalData: {
    // 服务端地址：
    //  - 开发调试: 本机运行 `longshun server 9000` 后使用 http://127.0.0.1:8080
    //    并在微信开发者工具中勾选「不校验合法域名、web-view…」
    //  - 生产环境: 换成已备案的 HTTPS 域名（需要反向代理到服务端 8080 端口）
    apiBase: 'http://127.0.0.1:8080',
    // 支付模式: mock=模拟支付(开发调试) / real=真实微信支付(需配置商户号)
    payMode: 'mock',
    userInfo: null
  },

  onLaunch() {
    const base = wx.getStorageSync('api_base')
    if (base) this.globalData.apiBase = base
  }
})
