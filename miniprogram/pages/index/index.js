// 首页：品牌展示 + 功能入口
Page({
  data: {
    features: [
      { key: 'voice', icon: '🎤', name: '语音助手', desc: '输入法语音对话 · 自动报价', path: '/pages/voice/voice' },
      { key: 'rooms', icon: '🏨', name: '客房报价', desc: '庭院小院 / 商务楼 · 24 档门市价', path: '/pages/rooms/rooms' },
      { key: 'venues', icon: '🏛', name: '会场报价', desc: '23 个会场 · 半天/全天/小时', path: '/pages/venues/venues' },
      { key: 'booking', icon: '📝', name: '在线预订', desc: '客房 / 会场快速下单', path: '/pages/booking/booking' },
      { key: 'orders', icon: '📋', name: '我的订单', desc: '预订记录与支付状态', path: '/pages/orders/orders' }
    ],
    notice: '龙顺庄园位于湖南长沙，集商务住宿、会议会展、餐饮宴会于一体。小程序支持客房与会场报价查询、在线预订及微信支付。'
  },

  onFeatureTap(e) {
    wx.navigateTo({ url: e.currentTarget.dataset.path })
  }
})
