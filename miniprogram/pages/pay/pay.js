// 订单支付：创建支付单 → 模拟支付 / 真实微信支付(wx.requestPayment)
const api = require('../../utils/api')

Page({
  data: {
    orderId: '',
    amount: '',
    status: 'pending',
    statusText: '待支付',
    paying: false,
    payment: null
  },

  onLoad(options) {
    this.setData({
      orderId: options.orderId || '',
      amount: options.amount || ''
    })
    this.createPayment()
  },

  createPayment() {
    const { orderId, amount } = this.data
    if (!orderId) return
    api
      .payCreate({
        order_id: orderId,
        amount: Number(amount || 0),
        subject: '龙顺庄园预订'
      })
      .then((res) => {
        if (res.code !== 0) {
          return wx.showToast({ title: res.msg || '下单失败', icon: 'none' })
        }
        this.setData({ payment: res.payment })
      })
      .catch(() => wx.showToast({ title: '网络错误', icon: 'none' }))
  },

  // 模拟支付（开发调试模式）
  onMockPay() {
    const { orderId, paying } = this.data
    if (paying) return
    this.setData({ paying: true })
    api
      .payMock(orderId)
      .then((res) => {
        this.setData({ paying: false })
        if (res.code !== 0) {
          return wx.showToast({ title: res.msg || '支付失败', icon: 'none' })
        }
        this.setData({ status: 'paid', statusText: '已支付' })
        wx.showToast({ title: '支付成功', icon: 'success' })
      })
      .catch(() => {
        this.setData({ paying: false })
        wx.showToast({ title: '网络错误', icon: 'none' })
      })
  },

  // 真实微信支付（需生产环境配置商户号并返回真实支付参数）
  onWechatPay() {
    const p = this.data.payment
    if (!p) return wx.showToast({ title: '支付参数缺失', icon: 'none' })
    const { paying } = this.data
    if (paying) return
    this.setData({ paying: true })
    wx.requestPayment({
      timeStamp: p.time_stamp,
      nonceStr: p.nonce_str,
      package: 'prepay_id=' + p.prepay_id,
      signType: p.sign_type || 'MD5',
      paySign: p.pay_sign,
      success: () => {
        this.setData({ paying: false, status: 'paid', statusText: '已支付' })
      },
      fail: (err) => {
        this.setData({ paying: false })
        if (err.errMsg && err.errMsg.indexOf('cancel') >= 0) {
          wx.showToast({ title: '已取消支付', icon: 'none' })
        } else {
          wx.showToast({ title: '支付失败', icon: 'none' })
        }
      }
    })
  },

  onQueryStatus() {
    const { orderId } = this.data
    if (!orderId) return
    api
      .payStatus(orderId)
      .then((res) => {
        if (res.code === 0) {
          this.setData({
            status: res.status,
            statusText: res.status === 'paid' ? '已支付' : '待支付'
          })
        } else {
          wx.showToast({ title: res.msg || '未查到订单', icon: 'none' })
        }
      })
      .catch(() => wx.showToast({ title: '网络错误', icon: 'none' }))
  },

  goOrders() {
    wx.redirectTo({ url: '/pages/orders/orders' })
  }
})
