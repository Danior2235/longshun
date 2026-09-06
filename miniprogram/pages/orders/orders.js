// 我的订单：预订记录 + 支付状态
const api = require('../../utils/api')

Page({
  data: {
    list: [],
    loading: false
  },

  onShow() {
    this.load()
  },

  onPullDownRefresh() {
    this.load().then(() => wx.stopPullDownRefresh())
  },

  load() {
    this.setData({ loading: true })
    return api
      .getBookings()
      .then((list) => {
        const rows = (list || []).map((o) => ({
          ...o,
          statusText: o.status === 'paid' ? '已支付' : '待支付',
          itemText: o.room_type || o.venue || '—',
          amountText: o.amount ? '¥' + o.amount : '—'
        }))
        this.setData({ list: rows, loading: false })
      })
      .catch(() => {
        this.setData({ loading: false })
        wx.showToast({ title: '加载失败', icon: 'none' })
      })
  },

  goPay(e) {
    const o = e.currentTarget.dataset.item
    if (o.status === 'paid') return
    wx.navigateTo({
      url:
        '/pages/pay/pay?orderId=' +
        encodeURIComponent(o.id) +
        '&amount=' +
        (o.amount || 0)
    })
  }
})
